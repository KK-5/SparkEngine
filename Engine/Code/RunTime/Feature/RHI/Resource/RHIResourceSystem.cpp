#include "RHIResourceSystem.h"

#include <Service/Service.h>
#include <Log/SpdLogSystem.h>
#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/HardwareQueue.h>

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
        m_hostReadbackImagePool.reset();
        m_deviceImagePool.reset();
        m_hostReadbackPlacedBufferPool.reset();
        m_hostUploadPlacedBufferPool.reset();
        m_deviceCommittedBufferPool.reset();
        m_devicePlacedBufferPool.reset();
    }

    void RHIResourceSystem::OnFrameBegin()
    {
        auto* rhi = Service<RHIInterface>::Get();
        auto* device = rhi->GetDevice();
        auto& ctx = *RHIExecuteContext::Current();

        CreateBuffers(ctx, *device);
        CreateImages(ctx, *device);
        CreateBufferViews(ctx, *device);
        CreateImageViews(ctx, *device);
    }

    BufferPool* RHIResourceSystem::SelectBufferPool(const BufferDescriptor& desc) const
    {
        const BufferBindFlags flags = desc.m_bindFlags;

        // Host upload: CopyRead | Constant
        constexpr auto hostUploadMask = BufferBindFlags::CopyRead | BufferBindFlags::Constant;
        if (flags != BufferBindFlags::None && (flags & hostUploadMask) == flags)
        {
            return m_hostUploadPlacedBufferPool.get();
        }

        // Host readback: CopyWrite
        if (flags != BufferBindFlags::None && (flags & BufferBindFlags::CopyWrite) == flags)
        {
            return m_hostReadbackPlacedBufferPool.get();
        }

        // Device committed: ray tracing acceleration structures / shader tables
        constexpr auto deviceCommittedMask = BufferBindFlags::RayTracingAccelerationStructure
                                            | BufferBindFlags::RayTracingShaderTable
                                            | BufferBindFlags::RayTracingScratchBuffer;
        if (flags != BufferBindFlags::None && (flags & deviceCommittedMask) == flags)
        {
            return m_deviceCommittedBufferPool.get();
        }

        // Device placed: catch-all for all remaining device-local flags
        return m_devicePlacedBufferPool.get();
    }

    ImagePool* RHIResourceSystem::SelectImagePool(const ImageDescriptor& desc) const
    {
        const ImageBindFlags flags = desc.m_bindFlags;

        // Host readback: CopyWrite only (GPU writes, CPU reads back)
        if (flags != ImageBindFlags::None && (flags & ImageBindFlags::CopyWrite) == flags)
        {
            return m_hostReadbackImagePool.get();
        }

        // Device: all GPU-resident images
        return m_deviceImagePool.get();
    }

    void RHIResourceSystem::CreateBuffers(RHIContext& ctx, Device& device)
    {
        auto* factory = Service<Factory>::Get();

        auto view = ctx.GetView<BufferDescriptor>(Exclude<Components::Buffer, Components::BufferPerFrame>);

        eastl::vector<RHIHandle> toDestroy;

        view.each([&](RHIHandle handle, const BufferDescriptor& desc)
        {
            BufferPool* pool = SelectBufferPool(desc);

            if (ctx.Has<PerFrameTag>(handle))
            {
                Components::BufferPerFrame perFrame;
                const uint32_t frameCount = device.GetDescriptor().m_frameCountMax;
                bool failed = false;
                for (uint32_t i = 0; i < frameCount; ++i)
                {
                    Ptr<RHI::Buffer> buffer = factory->CreateBuffer();
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
                if (pool->InitBuffer(BufferInitRequest{*buffer, desc}) != ResultCode::Success)
                {
                    LOG_ERROR("[RHIResourceSystem] InitBuffer failed (entity {}); destroying entity.",
                              static_cast<uint32_t>(handle));
                    toDestroy.push_back(handle);
                    return;
                }
                ctx.Add<Components::Buffer>(handle, Components::Buffer{ buffer });
            }
        });

        for (RHIHandle handle : toDestroy)
        {
            ctx.DestoryEntity(handle);
        }
    }

    void RHIResourceSystem::CreateImages(RHIContext& ctx, Device& device)
    {
        auto* factory = Service<Factory>::Get();

        auto view = ctx.GetView<ImageDescriptor>(Exclude<Components::Image, Components::ImagePerFrame>);

        eastl::vector<RHIHandle> toDestroy;

        view.each([&](RHIHandle handle, const ImageDescriptor& desc)
        {
            ImagePool* pool = SelectImagePool(desc);

            if (ctx.Has<PerFrameTag>(handle))
            {
                Components::ImagePerFrame perFrame;
                const uint32_t frameCount = device.GetDescriptor().m_frameCountMax;
                bool failed = false;
                for (uint32_t i = 0; i < frameCount; ++i)
                {
                    Ptr<RHI::Image> image = factory->CreateImage();
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
        });

        for (RHIHandle handle : toDestroy)
        {
            ctx.DestoryEntity(handle);
        }
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
            }
        });

        for (RHIHandle handle : toDestroy)
        {
            ctx.DestoryEntity(handle);
        }
    }
}
