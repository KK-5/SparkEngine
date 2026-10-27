// ReservedImage — a reserved depth image array with tiles mapped from a tile backing.
//
// Headless: a 512x512 D32 array of two slices is reserved, and the top left 2x2 tiles of slice 1
// are mapped onto the four tiles of a 256x256 backing. A render pass clears slice 1 through a
// depth stencil view and a copy reads it back: the mapped tiles give the clear value, the rest
// zero (the clear's writes to them are dropped). Then the tiles are unmapped, slice 1 is cleared
// again and reads back zero throughout, and the backing is released.

#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <Base.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/HardwareQueue.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Command/ImageTileMapping.h>
#include <RHI/Command/CommandQueue.h>
#include <RHI/Command/CommandRecorder.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Command/CopyItem.h>
#include <RHI/Command/RenderPassBeginInfo.h>
#include <RHI/Resource/ResourceState.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Backend/DX12/RHISystem.h>

#include <cstdint>

using namespace Spark;

namespace
{
    constexpr uint32_t kSize          = 512;
    constexpr uint16_t kSliceCount    = 2;
    constexpr uint16_t kMappedSlice   = 1;
    constexpr uint32_t kBackingSize   = 256;
    constexpr RHI::Format kFormat     = RHI::Format::D32_FLOAT;
    constexpr RHI::ImageBindFlags kReservedFlags =
        RHI::ImageBindFlags::DepthStencil | RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyRead;

    Ptr<RHI::ImagePool> InitImagePool(RHI::Factory& factory, RHI::Device& device, RHI::ImageBindFlags bindFlags)
    {
        RHI::ImagePoolDescriptor descriptor;
        descriptor.m_bindFlags = bindFlags;
        Ptr<RHI::ImagePool> pool = factory.CreateImagePool();
        if (pool->Init(device, descriptor) != RHI::ResultCode::Success)
        {
            return nullptr;
        }
        return pool;
    }

    Ptr<RHI::Image> InitImage(RHI::Factory& factory, RHI::ImagePool& pool, const RHI::ImageDescriptor& descriptor, const char* name)
    {
        Ptr<RHI::Image> image = factory.CreateImage();
        image->SetName(ObjectName(name));
        RHI::ImageInitRequest request;
        request.m_image      = image.get();
        request.m_descriptor = descriptor;
        if (pool.InitImage(request) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedImage] Image {} init failed.", name);
            return nullptr;
        }
        return image;
    }

    Ptr<RHI::ImageView> InitView(RHI::Factory& factory, const RHI::Image& image, const RHI::ImageViewDescriptor& descriptor)
    {
        Ptr<RHI::ImageView> view = factory.CreateImageView();
        if (view->Init(image, descriptor) != RHI::ResultCode::Success)
        {
            return nullptr;
        }
        return view;
    }

    void Submit(RHI::CommandQueue& queue, RHI::CommandRecorder& recorder, RHI::CommandList& commandList, RHI::Fence& fence)
    {
        commandList.Close();
        RHI::CommandList* lists[] = { &commandList };
        queue.ExecuteCommands(lists);
        queue.FlushCommands(fence);
        recorder.Reset();
    }
}

int main(int, char**)
{
    LogConfig logConfig{};
    logConfig.m_showTimeStamp = true;
    UniquePtr<ILogSystem> logger = eastl::make_unique<SpdLogSystem>(logConfig);

    // --- RHI device (headless) ---
    auto rhiSystem = CreateSystem<RHI::DX12::RHISystem>();
    rhiSystem->Init();
    auto* rhi = Service<RHI::RHIInterface>::Get();
    RHI::PhysicalDeviceList devices = rhi->EnumeratePhysicalDevices();
    if (devices.empty())
    {
        LOG_ERROR("[ReservedImage] No physical devices.");
        return 1;
    }
    RHI::DeviceDescriptor devDesc;
    devDesc.m_frameCountMax = 2;
    if (rhi->InitDevice(*devices[0], devDesc) != RHI::ResultCode::Success)
    {
        LOG_ERROR("[ReservedImage] InitDevice failed.");
        return 1;
    }
    auto* factory = rhi->GetRHIFactory();
    auto* device  = rhi->GetDevice();

    // --- Queue / recorder / fence ---
    Ptr<RHI::CommandQueue> queue = factory->CreateCommandQueue();
    {
        RHI::CommandQueueDescriptor desc;
        desc.m_hardwareQueueClass = RHI::HardwareQueueClass::Graphics;
        desc.m_maxFrameQueueDepth = 1;
        if (queue->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedImage] Command queue init failed.");
            return 1;
        }
    }

    Ptr<RHI::CommandRecorder> recorder = factory->CreateCommandRecorder();
    {
        RHI::CommandRecorderDescriptor desc;
        desc.m_queue = RHI::HardwareQueueClass::Graphics;
        if (recorder->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedImage] Command recorder init failed.");
            return 1;
        }
    }

    Ptr<RHI::Fence> fence = factory->CreateFence();
    if (fence->Init(*device, RHI::FenceState::Reset) != RHI::ResultCode::Success)
    {
        LOG_ERROR("[ReservedImage] Fence init failed.");
        return 1;
    }

    // --- Pools: the reserved image's, its tile backings', and the readback's ---
    Ptr<RHI::ImagePool> reservedPool = InitImagePool(*factory, *device, kReservedFlags);
    Ptr<RHI::ImagePool> backingPool  = InitImagePool(*factory, *device, kReservedFlags | RHI::ImageBindFlags::TileBacking);

    const uint32_t bytesPerRow   = kSize * sizeof(float);
    const uint64_t bytesPerSlice = static_cast<uint64_t>(bytesPerRow) * kSize;
    Ptr<RHI::BufferPool> readbackPool = factory->CreateBufferPool();
    {
        RHI::BufferPoolDescriptor desc;
        desc.m_heapMemoryLevel  = RHI::HeapMemoryLevel::Host;
        desc.m_hostMemoryAccess = RHI::HostMemoryAccess::Read;
        desc.m_bindFlags        = RHI::BufferBindFlags::CopyWrite;
        if (readbackPool->Init(*device, desc) != RHI::ResultCode::Success)
        {
            readbackPool.reset();
        }
    }

    if (!reservedPool || !backingPool || !readbackPool)
    {
        LOG_ERROR("[ReservedImage] Pool init failed.");
        return 1;
    }

    // --- Images ---
    RHI::ImageDescriptor reservedDesc = RHI::ImageDescriptor::Create2DArray(kReservedFlags, kSize, kSize, kSliceCount, kFormat);
    reservedDesc.m_reserved = true;
    Ptr<RHI::Image> reserved = InitImage(*factory, *reservedPool, reservedDesc, "Reserved");

    Ptr<RHI::Image> backing = InitImage(*factory, *backingPool,
        RHI::ImageDescriptor::Create2D(kReservedFlags | RHI::ImageBindFlags::TileBacking, kBackingSize, kBackingSize, kFormat),
        "Backing");

    Ptr<RHI::Buffer> readback = factory->CreateBuffer();
    readback->SetName(ObjectName("Readback"));
    {
        RHI::BufferInitRequest request;
        request.m_buffer     = readback.get();
        request.m_descriptor = RHI::BufferDescriptor(RHI::BufferBindFlags::CopyWrite, bytesPerSlice);
        if (readbackPool->InitBuffer(request) != RHI::ResultCode::Success)
        {
            readback.reset();
        }
    }

    if (!reserved || !backing || !readback)
    {
        LOG_ERROR("[ReservedImage] Resource init failed.");
        return 1;
    }

    const RHI::Size& tileShape = reserved->GetTileShape();
    LOG_INFO("[ReservedImage] Tiles are {}x{}x{} texels.", tileShape.m_width, tileShape.m_height, tileShape.m_depth);
    if (tileShape.m_width == 0 || kBackingSize % tileShape.m_width != 0 || kBackingSize % tileShape.m_height != 0)
    {
        LOG_ERROR("[ReservedImage] The backing is not a whole number of tiles.");
        return 1;
    }
    const uint32_t mappedTilesX = kBackingSize / tileShape.m_width;
    const uint32_t mappedTilesY = kBackingSize / tileShape.m_height;

    // --- Views: a depth stencil view of the mapped slice, a shader view of the array ---
    RHI::ImageViewDescriptor depthDesc = RHI::ImageViewDescriptor::Create(
        RHI::Format::Unknown, 0, 0, kMappedSlice, kMappedSlice);
    depthDesc.m_overrideBindFlags = RHI::ImageBindFlags::DepthStencil;
    Ptr<RHI::ImageView> depthView = InitView(*factory, *reserved, depthDesc);

    RHI::ImageViewDescriptor shaderDesc = RHI::ImageViewDescriptor::Create(
        RHI::Format::R32_FLOAT, 0, 0, 0, kSliceCount - 1);
    shaderDesc.m_overrideBindFlags = RHI::ImageBindFlags::ShaderRead;
    shaderDesc.m_isArray           = 1;
    Ptr<RHI::ImageView> shaderView = InitView(*factory, *reserved, shaderDesc);

    if (!depthView || !shaderView)
    {
        LOG_ERROR("[ReservedImage] Image view init failed.");
        return 1;
    }

    // The top left tiles of the mapped slice; no backing unmaps them.
    auto mapTiles = [&](const RHI::Image* backingImage)
    {
        RHI::ImageTileMapping mapping;
        mapping.m_image             = reserved.get();
        mapping.m_mipSlice          = 0;
        mapping.m_arraySlice        = kMappedSlice;
        mapping.m_tileOrigin        = RHI::Origin(0, 0, 0);
        mapping.m_tileExtent        = RHI::Size(mappedTilesX, mappedTilesY, 1);
        mapping.m_backingImage      = backingImage;
        mapping.m_backingTileOffset = 0;
        const bool updated =
            queue->UpdateTileMappings(eastl::span<const RHI::ImageTileMapping>(&mapping, 1)) == RHI::ResultCode::Success;
        queue->FlushCommands(*fence);
        return updated;
    };

    // Clears the mapped slice to `depth` through the depth stencil view and reads it back; true
    // when the texels of the backing's square read `depth` and every other one zero.
    auto clearAndCheck = [&](float depth, bool mapped, const char* what)
    {
        RHI::CommandList* cmd = recorder->GetCommandList();

        cmd->QueueBarrier(RHI::ConvertToDepthStencilWrite(*reserved));
        cmd->FlushBarriers();
        {
            RHI::RenderPassBeginInfo pass;
            pass.m_depthStencilAttachment.m_view = depthView.get();
            pass.m_depthStencilAttachment.m_loadStoreAction.m_clearValue  = RHI::ClearValue::CreateDepth(depth);
            pass.m_depthStencilAttachment.m_loadStoreAction.m_loadAction  = RHI::AttachmentLoadAction::Clear;
            pass.m_depthStencilAttachment.m_loadStoreAction.m_storeAction = RHI::AttachmentStoreAction::Store;
            cmd->BeginRenderPass(pass);
            cmd->EndRenderPass();
        }

        cmd->QueueBarrier(RHI::ConvertToImageCopyRead(*reserved));
        cmd->FlushBarriers();
        {
            RHI::CopyImageToBufferDescriptor copy;
            copy.m_sourceImage              = reserved.get();
            copy.m_sourceSubresource        = RHI::ImageSubresource(0, kMappedSlice, RHI::ImageAspect::Depth);
            copy.m_sourceSize               = RHI::Size(kSize, kSize, 1);
            copy.m_destinationBuffer        = readback.get();
            copy.m_destinationBytesPerRow   = bytesPerRow;
            copy.m_destinationBytesPerImage = static_cast<uint32_t>(bytesPerSlice);
            copy.m_destinationFormat        = kFormat;
            cmd->Submit(RHI::CopyItem(copy));
        }

        Submit(*queue, *recorder, *cmd, *fence);

        RHI::BufferMapRequest mapRequest(*readback, 0, bytesPerSlice);
        RHI::BufferMapResponse mapResponse;
        if (readbackPool->MapBuffer(mapRequest, mapResponse) != RHI::ResultCode::Success || !mapResponse.m_data)
        {
            LOG_ERROR("[ReservedImage] Readback map failed.");
            return false;
        }
        const auto* texels = static_cast<const float*>(mapResponse.m_data);
        uint32_t wrongInside  = 0;
        uint32_t wrongOutside = 0;
        for (uint32_t y = 0; y < kSize; ++y)
        {
            for (uint32_t x = 0; x < kSize; ++x)
            {
                const bool  inside   = mapped && x < kBackingSize && y < kBackingSize;
                const float expected = inside ? depth : 0.0f;
                if (texels[y * kSize + x] != expected)
                {
                    ++(inside ? wrongInside : wrongOutside);
                }
            }
        }
        readbackPool->UnmapBuffer(*readback);

        if (wrongInside != 0 || wrongOutside != 0)
        {
            LOG_ERROR("[ReservedImage] {}: {} mapped and {} unmapped texels wrong.", what, wrongInside, wrongOutside);
            return false;
        }
        LOG_INFO("[ReservedImage] {}: reads as expected.", what);
        return true;
    };

    bool passed = true;

    if (!mapTiles(backing.get()))
    {
        LOG_ERROR("[ReservedImage] Mapping the tiles failed.");
        return 1;
    }
    passed &= clearAndCheck(0.5f, true, "Mapped");

    if (!mapTiles(nullptr))
    {
        LOG_ERROR("[ReservedImage] Unmapping the tiles failed.");
        return 1;
    }
    passed &= clearAndCheck(0.75f, false, "Unmapped");

    // Unmapped and flushed, so the backing can go.
    backing.reset();

    if (!passed)
    {
        LOG_ERROR("[ReservedImage] FAILED.");
        return 1;
    }
    LOG_INFO("[ReservedImage] PASSED.");
    return 0;
}
