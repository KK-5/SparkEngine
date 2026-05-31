#include "RHIResourceSystem.h"

#include <Service/Service.h>
#include <Log/ILogSystem.h>
#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/HardwareQueue.h>
#include <cstring>

namespace Spark::RHI
{
    void RHIResourceSystem::InitInternal()
    {
        auto* rhi = Service<RHIInterface>::Get();
        auto* factory = rhi->GetRHIFactory();
        auto* device = rhi->GetDevice();

        // Device placed pool — sub-allocated GPU-resident buffers
        {
            BufferPoolDescriptor desc;
            desc.m_heapMemoryLevel = HeapMemoryLevel::Device;
            desc.m_bindFlags = BufferBindFlags::InputAssembly
                             | BufferBindFlags::DynamicInputAssembly
                             | BufferBindFlags::ShaderRead
                             | BufferBindFlags::ShaderWrite
                             | BufferBindFlags::CopyWrite
                             | BufferBindFlags::Indirect
                             | BufferBindFlags::Predication;
            desc.m_sharedQueueMask = HardwareQueueClassMask::All;
            m_devicePlacedBufferPool = factory->CreateBufferPool();
            m_devicePlacedBufferPool->Init(*device, desc);
        }

        // Device committed pool — oversized / ray-tracing escape hatch
        {
            BufferPoolDescriptor desc;
            desc.m_heapMemoryLevel = HeapMemoryLevel::Device;
            desc.m_bindFlags = BufferBindFlags::RayTracingAccelerationStructure
                             | BufferBindFlags::RayTracingShaderTable
                             | BufferBindFlags::RayTracingScratchBuffer;
            desc.m_sharedQueueMask = HardwareQueueClassMask::All;
            m_deviceCommittedBufferPool = factory->CreateBufferPool();
            m_deviceCommittedBufferPool->Init(*device, desc);
        }

        // Host upload placed pool — CPU-writable dynamic buffers (per-frame StructuredBuffer, cbuffer)
        {
            BufferPoolDescriptor desc;
            desc.m_heapMemoryLevel = HeapMemoryLevel::Host;
            desc.m_hostMemoryAccess = HostMemoryAccess::Write;
            desc.m_bindFlags = BufferBindFlags::CopyRead | BufferBindFlags::Constant;
            desc.m_sharedQueueMask = HardwareQueueClassMask::All;
            m_hostUploadPlacedBufferPool = factory->CreateBufferPool();
            m_hostUploadPlacedBufferPool->Init(*device, desc);
        }

        // Host readback placed pool — GPU-written, CPU-read buffers
        {
            BufferPoolDescriptor desc;
            desc.m_heapMemoryLevel = HeapMemoryLevel::Host;
            desc.m_hostMemoryAccess = HostMemoryAccess::Read;
            desc.m_bindFlags = BufferBindFlags::CopyWrite;
            desc.m_sharedQueueMask = HardwareQueueClassMask::All;
            m_hostReadbackPlacedBufferPool = factory->CreateBufferPool();
            m_hostReadbackPlacedBufferPool->Init(*device, desc);
        }

        // Device image pool — GPU-resident images (textures/RenderTarget/DepthStencil/UAV)
        {
            ImagePoolDescriptor desc;
            desc.m_bindFlags = ImageBindFlags::ShaderRead | ImageBindFlags::ShaderWrite
                             | ImageBindFlags::Color | ImageBindFlags::DepthStencil
                             | ImageBindFlags::CopyRead | ImageBindFlags::CopyWrite
                             | ImageBindFlags::ShadingRate;
            m_deviceImagePool = factory->CreateImagePool();
            m_deviceImagePool->Init(*device, desc);
        }

        // Host readback image pool — GPU-written, CPU-read images (screenshots, etc.)
        {
            ImagePoolDescriptor desc;
            desc.m_heapMemoryLevel = HeapMemoryLevel::Host;
            desc.m_hostMemoryAccess = HostMemoryAccess::Read;
            desc.m_bindFlags = ImageBindFlags::CopyWrite;
            m_hostReadbackImagePool = factory->CreateImagePool();
            m_hostReadbackImagePool->Init(*device, desc);
        }

        FrameEventBus::Handler::BusConnect();
    }

    void RHIResourceSystem::ShutdownInternal()
    {
        FrameEventBus::Handler::BusDisconnect();
    }

    void RHIResourceSystem::OnFrameBegin()
    {
        auto* rhi = Service<RHIInterface>::Get();
        auto* device = rhi->GetDevice();
        auto& ctx = *RHIExecuteContext::Current();

        CreateBuffers(ctx, *device);
        CreateImages(ctx, *device);
        ProcessBufferMaps(ctx);
        CreateBufferViews(ctx, *device);
        CreateImageViews(ctx, *device);

        m_frameIndex = (m_frameIndex + 1) % device->GetDescriptor().m_frameCountMax;
    }

    BufferPool* RHIResourceSystem::SelectBufferPool(const PendingBufferInit& init) const
    {
        if (init.m_heapMemoryLevel == HeapMemoryLevel::Host)
        {
            if (init.m_hostMemoryAccess == HostMemoryAccess::Read)
                return m_hostReadbackPlacedBufferPool.get();
            return m_hostUploadPlacedBufferPool.get();
        }

        // Device heap: check for RT committed allocation
        const BufferBindFlags flags = init.m_descriptor.m_bindFlags;
        constexpr auto rtMask = BufferBindFlags::RayTracingAccelerationStructure
                              | BufferBindFlags::RayTracingShaderTable
                              | BufferBindFlags::RayTracingScratchBuffer;
        if (flags != BufferBindFlags::None && (flags & rtMask) == flags)
            return m_deviceCommittedBufferPool.get();

        return m_devicePlacedBufferPool.get();
    }

    ImagePool* RHIResourceSystem::SelectImagePool(const PendingImageInit& init) const
    {
        if (init.m_heapMemoryLevel == HeapMemoryLevel::Host)
        {
            // Host-write images have no realistic use case in graphics rendering
            // (uploads go through staging buffers + copy queue). Only readback is
            // supported on the host heap.
            if (init.m_hostMemoryAccess != HostMemoryAccess::Read)
                return nullptr;
            return m_hostReadbackImagePool.get();
        }

        return m_deviceImagePool.get();
    }

    void RHIResourceSystem::CreateBuffers(RHIContext& ctx, Device& device)
    {
        auto* factory = Service<Factory>::Get();

        auto view = ctx.GetView<PendingBufferInit>(Exclude<Components::Buffer, Components::BufferPerFrame>);

        eastl::vector<RHIHandle> toDestroy;

        view.each([&](RHIHandle handle, const PendingBufferInit& init)
        {
            BufferPool* pool = SelectBufferPool(init);
            const BufferDescriptor& desc = init.m_descriptor;

            if (ctx.Has<PerFrameTag>(handle))
            {
                Components::BufferPerFrame perFrame;
                const uint32_t frameCount = device.GetDescriptor().m_frameCountMax;
                bool failed = false;
                for (uint32_t i = 0; i < frameCount; ++i)
                {
                    Ptr<RHI::Buffer> buffer = factory->CreateBuffer();
                    if (auto* nameComp = ctx.TryGet<ResourceName>(handle))
                    {
                        buffer->SetName(nameComp->m_name);
                    }
                    if (pool->InitBuffer(BufferInitRequest{*buffer, desc}) != ResultCode::Success)
                    {
                        LOG_ERROR("[RHIResourceSystem] InitBuffer failed for per-frame buffer "
                                  "(entity {}); destroying entity.", static_cast<uint32_t>(handle));
                        failed = true;
                        break;
                    }
                    perFrame.m_buffers[i] = eastl::move(buffer);
                }
                if (failed)
                {
                    toDestroy.push_back(handle);
                    return;
                }
                ctx.Add<Components::BufferPerFrame>(handle, eastl::move(perFrame));
            }
            else
            {
                Ptr<RHI::Buffer> buffer = factory->CreateBuffer();
                if (auto* nameComp = ctx.TryGet<ResourceName>(handle))
                {
                    buffer->SetName(nameComp->m_name);
                }
                if (pool->InitBuffer(BufferInitRequest{*buffer, desc}) != ResultCode::Success)
                {
                    LOG_ERROR("[RHIResourceSystem] InitBuffer failed (entity {}); destroying entity.",
                              static_cast<uint32_t>(handle));
                    toDestroy.push_back(handle);
                    return;
                }
                ctx.Add<Components::Buffer>(handle, Components::Buffer{ buffer });
            }

            ctx.Remove<PendingBufferInit>(handle);
        });

        for (RHIHandle handle : toDestroy)
        {
            ctx.DestoryEntity(handle);
        }
    }

    void RHIResourceSystem::CreateImages(RHIContext& ctx, Device& device)
    {
        auto* factory = Service<Factory>::Get();

        auto view = ctx.GetView<PendingImageInit>(Exclude<Components::Image, Components::ImagePerFrame>);

        eastl::vector<RHIHandle> toDestroy;

        view.each([&](RHIHandle handle, const PendingImageInit& init)
        {
            ImagePool* pool = SelectImagePool(init);
            if (!pool)
            {
                LOG_ERROR("[RHIResourceSystem] No image pool for entity {} "
                          "(HeapMemoryLevel=Host with HostMemoryAccess=Write is not supported); "
                          "destroying entity.", static_cast<uint32_t>(handle));
                toDestroy.push_back(handle);
                return;
            }
            const ImageDescriptor& desc = init.m_descriptor;

            if (ctx.Has<PerFrameTag>(handle))
            {
                Components::ImagePerFrame perFrame;
                const uint32_t frameCount = device.GetDescriptor().m_frameCountMax;
                bool failed = false;
                for (uint32_t i = 0; i < frameCount; ++i)
                {
                    Ptr<RHI::Image> image = factory->CreateImage();
                    if (auto* nameComp = ctx.TryGet<ResourceName>(handle))
                    {
                        image->SetName(nameComp->m_name);
                    }
                    ImageInitRequest request;
                    request.m_image = image.get();
                    request.m_descriptor = desc;
                    if (pool->InitImage(request) != ResultCode::Success)
                    {
                        LOG_ERROR("[RHIResourceSystem] InitImage failed for per-frame image "
                                  "(entity {}); destroying entity.", static_cast<uint32_t>(handle));
                        failed = true;
                        break;
                    }
                    perFrame.m_images[i] = eastl::move(image);
                }
                if (failed)
                {
                    toDestroy.push_back(handle);
                    return;
                }
                ctx.Add<Components::ImagePerFrame>(handle, eastl::move(perFrame));
            }
            else
            {
                Ptr<RHI::Image> image = factory->CreateImage();
                if (auto* nameComp = ctx.TryGet<ResourceName>(handle))
                {
                    image->SetName(nameComp->m_name);
                }
                ImageInitRequest request;
                request.m_image = image.get();
                request.m_descriptor = desc;
                if (pool->InitImage(request) != ResultCode::Success)
                {
                    LOG_ERROR("[RHIResourceSystem] InitImage failed (entity {}); destroying entity.",
                              static_cast<uint32_t>(handle));
                    toDestroy.push_back(handle);
                    return;
                }
                ctx.Add<Components::Image>(handle, Components::Image{ image });
            }

            ctx.Remove<PendingImageInit>(handle);
        });

        for (RHIHandle handle : toDestroy)
        {
            ctx.DestoryEntity(handle);
        }
    }

    void RHIResourceSystem::ProcessBufferMaps(RHIContext& ctx)
    {
        auto process = [&](RHIHandle handle, RHI::Buffer& buffer, const PendingBufferMap& mapReq)
        {
            auto* pool = static_cast<BufferPool*>(buffer.GetPool());
            if (!pool)
            {
                LOG_ERROR("[RHIResourceSystem] ProcessBufferMaps: buffer on entity {} has no pool; skipping.",
                          static_cast<uint32_t>(handle));
                return;
            }

            if (pool->GetDescriptor().m_heapMemoryLevel != HeapMemoryLevel::Host)
            {
                LOG_ERROR("[RHIResourceSystem] ProcessBufferMaps: buffer on entity {} is Device heap; "
                          "use PendingBufferUpload for Device buffers.",
                          static_cast<uint32_t>(handle));
                return;
            }

            BufferMapRequest request(buffer, mapReq.m_byteOffset, mapReq.m_byteCount);
            BufferMapResponse response;
            if (pool->MapBuffer(request, response) != ResultCode::Success)
            {
                LOG_ERROR("[RHIResourceSystem] MapBuffer failed for entity {}.",
                          static_cast<uint32_t>(handle));
                return;
            }

            memcpy(response.m_data, mapReq.m_data, mapReq.m_byteCount);
            pool->UnmapBuffer(buffer);
        };

        // Single-frame buffers: Components::Buffer + PendingBufferMap
        {
            auto view = ctx.GetView<Components::Buffer, PendingBufferMap>();
            view.each([&](RHIHandle handle, const Components::Buffer& buf, const PendingBufferMap& mapReq)
            {
                process(handle, *buf.m_buffer, mapReq);
                ctx.Remove<PendingBufferMap>(handle);
            });
        }

        // Per-frame buffers: Components::BufferPerFrame + PendingBufferMap
        // Writes only to the current frame's buffer, consistent with the
        // per-frame model where each frame targets a distinct copy.
        {
            auto view = ctx.GetView<Components::BufferPerFrame, PendingBufferMap>();
            view.each([&](RHIHandle handle, const Components::BufferPerFrame& perFrame,
                          const PendingBufferMap& mapReq)
            {
                process(handle, *perFrame.m_buffers[m_frameIndex], mapReq);
                ctx.Remove<PendingBufferMap>(handle);
            });
        }
    }

    void RHIResourceSystem::LinkViewToResource(RHIContext& ctx, RHIHandle viewEntity, RHIHandle resourceEntity)
    {
        // Head-insert viewEntity into resourceEntity's view linked list.
        ViewHierarchy& viewHierarchy = ctx.Get<ViewHierarchy>(viewEntity);
        viewHierarchy.m_prevView = NullHandle;

        ResourceHierarchy* resHierarchy = ctx.TryGet<ResourceHierarchy>(resourceEntity);
        if (!resHierarchy)
        {
            ctx.Add<ResourceHierarchy>(resourceEntity, ResourceHierarchy{});
            resHierarchy = ctx.TryGet<ResourceHierarchy>(resourceEntity);
        }

        viewHierarchy.m_nextView = resHierarchy->m_firstView;

        if (resHierarchy->m_firstView != NullHandle)
        {
            ViewHierarchy& nextHierarchy = ctx.Get<ViewHierarchy>(resHierarchy->m_firstView);
            nextHierarchy.m_prevView = viewEntity;
        }

        resHierarchy->m_firstView = viewEntity;
    }

    void RHIResourceSystem::CreateBufferViews(RHIContext& ctx, Device& device)
    {
        auto* factory = Service<Factory>::Get();

        auto view = ctx.GetView<BufferViewDescriptor, ViewHierarchy>(
            Exclude<Components::BufferView, Components::BufferViewPerFrame>);

        eastl::vector<RHIHandle> toDestroy;

        view.each([&](RHIHandle handle, const BufferViewDescriptor& viewDesc, const ViewHierarchy& hierarchy)
        {
            const RHIHandle resourceEntity = hierarchy.m_resource;

            // Resource entity already gone (e.g., materialization failed and was
            // destroyed) — the view can never resolve, drop it.
            if (resourceEntity == NullHandle || !ctx.Valid(resourceEntity))
            {
                LOG_ERROR("[RHIResourceSystem] BufferView entity {} references a destroyed "
                          "resource entity; destroying view.", static_cast<uint32_t>(handle));
                toDestroy.push_back(handle);
                return;
            }

            // Check if the underlying resource is single-frame or per-frame.
            if (auto* buffer = ctx.TryGet<Components::Buffer>(resourceEntity))
            {
                Ptr<RHI::BufferView> bufferView = factory->CreateBufferView();
                if (bufferView->Init(*buffer->m_buffer, viewDesc) != ResultCode::Success)
                {
                    LOG_ERROR("[RHIResourceSystem] BufferView::Init failed (entity {}); destroying entity.",
                              static_cast<uint32_t>(handle));
                    toDestroy.push_back(handle);
                    return;
                }
                ctx.Add<Components::BufferView>(handle, Components::BufferView{ bufferView });
                LinkViewToResource(ctx, handle, resourceEntity);
            }
            else if (auto* perFrame = ctx.TryGet<Components::BufferPerFrame>(resourceEntity))
            {
                const uint32_t frameCount = device.GetDescriptor().m_frameCountMax;
                Components::BufferViewPerFrame viewPerFrame;
                bool failed = false;
                for (uint32_t i = 0; i < frameCount; ++i)
                {
                    Ptr<RHI::BufferView> bufferView = factory->CreateBufferView();
                    if (bufferView->Init(*perFrame->m_buffers[i], viewDesc) != ResultCode::Success)
                    {
                        LOG_ERROR("[RHIResourceSystem] BufferView::Init failed for per-frame view "
                                  "(entity {}); destroying entity.", static_cast<uint32_t>(handle));
                        failed = true;
                        break;
                    }
                    viewPerFrame.m_views[i] = eastl::move(bufferView);
                }
                if (failed)
                {
                    toDestroy.push_back(handle);
                    return;
                }
                ctx.Add<Components::BufferViewPerFrame>(handle, eastl::move(viewPerFrame));
                LinkViewToResource(ctx, handle, resourceEntity);
            }
        });

        for (RHIHandle handle : toDestroy)
        {
            ctx.DestoryEntity(handle);
        }
    }

    void RHIResourceSystem::CreateImageViews(RHIContext& ctx, Device& device)
    {
        auto* factory = Service<Factory>::Get();

        auto view = ctx.GetView<ImageViewDescriptor, ViewHierarchy>(
            Exclude<Components::ImageView, Components::ImageViewPerFrame>);

        eastl::vector<RHIHandle> toDestroy;

        view.each([&](RHIHandle handle, const ImageViewDescriptor& viewDesc, const ViewHierarchy& hierarchy)
        {
            const RHIHandle resourceEntity = hierarchy.m_resource;

            if (resourceEntity == NullHandle || !ctx.Valid(resourceEntity))
            {
                LOG_ERROR("[RHIResourceSystem] ImageView entity {} references a destroyed "
                          "resource entity; destroying view.", static_cast<uint32_t>(handle));
                toDestroy.push_back(handle);
                return;
            }

            if (auto* image = ctx.TryGet<Components::Image>(resourceEntity))
            {
                Ptr<RHI::ImageView> imageView = factory->CreateImageView();
                if (imageView->Init(*image->m_image, viewDesc) != ResultCode::Success)
                {
                    LOG_ERROR("[RHIResourceSystem] ImageView::Init failed (entity {}); destroying entity.",
                              static_cast<uint32_t>(handle));
                    toDestroy.push_back(handle);
                    return;
                }
                ctx.Add<Components::ImageView>(handle, Components::ImageView{ imageView });
                LinkViewToResource(ctx, handle, resourceEntity);
            }
            else if (auto* perFrame = ctx.TryGet<Components::ImagePerFrame>(resourceEntity))
            {
                const uint32_t frameCount = device.GetDescriptor().m_frameCountMax;
                Components::ImageViewPerFrame viewPerFrame;
                bool failed = false;
                for (uint32_t i = 0; i < frameCount; ++i)
                {
                    Ptr<RHI::ImageView> imageView = factory->CreateImageView();
                    if (imageView->Init(*perFrame->m_images[i], viewDesc) != ResultCode::Success)
                    {
                        LOG_ERROR("[RHIResourceSystem] ImageView::Init failed for per-frame view "
                                  "(entity {}); destroying entity.", static_cast<uint32_t>(handle));
                        failed = true;
                        break;
                    }
                    viewPerFrame.m_views[i] = eastl::move(imageView);
                }
                if (failed)
                {
                    toDestroy.push_back(handle);
                    return;
                }
                ctx.Add<Components::ImageViewPerFrame>(handle, eastl::move(viewPerFrame));
                LinkViewToResource(ctx, handle, resourceEntity);
            }
        });

        for (RHIHandle handle : toDestroy)
        {
            ctx.DestoryEntity(handle);
        }
    }
}
