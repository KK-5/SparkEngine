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
        // Views are no longer materialized as entities — consumers resolve them on
        // demand from each resource's view cache via GetOrCreateImageView /
        // GetOrCreateImageViewPerFrame (see ResourceBuilder.h).

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
}
