// ReservedBuffer — a reserved buffer with pages mapped from a backing buffer.
//
// Headless: three pages reserved, the first and the last mapped onto the two pages of one
// backing buffer, the middle left unmapped. A copy writes all three, a compute shader reads
// them back through a view: the mapped pages give what was copied, the unmapped one zero (the
// copy's writes to it are dropped). Then the pages are unmapped and the backing released.

#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <Base.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/HardwareQueue.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Command/BufferTileMapping.h>
#include <RHI/Command/CommandQueue.h>
#include <RHI/Command/CommandRecorder.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Command/DispatchItem.h>
#include <RHI/Command/CopyItem.h>
#include <RHI/Pipeline/PipelineLibrary.h>
#include <RHI/Pipeline/PipelineState.h>
#include <RHI/Pipeline/PipelineStateDescriptor.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ResourceState.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Resource/ShaderInput/ShaderInput.h>
#include <RHI/Resource/ShaderInput/ShaderInputCompiler.h>
#include <RHI/Backend/DX12/RHISystem.h>

#include <Resource/AssetManager.h>
#include <VFS/VFSSystem.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderBuilder.h>

#include <EASTL/vector.h>
#include <cstdint>

using namespace Spark;

namespace
{
    constexpr uint64_t kReservedPageCount = 3;
    constexpr uint64_t kBackingPageCount  = 2;
    constexpr uint32_t kGroupSize         = 64;

    Ptr<RHI::BufferPool> InitPool(RHI::Factory& factory, RHI::Device& device, const RHI::BufferPoolDescriptor& descriptor)
    {
        Ptr<RHI::BufferPool> pool = factory.CreateBufferPool();
        if (pool->Init(device, descriptor) != RHI::ResultCode::Success)
        {
            return nullptr;
        }
        return pool;
    }

    Ptr<RHI::Buffer> InitBuffer(RHI::Factory& factory, RHI::BufferPool& pool, const RHI::BufferDescriptor& descriptor, const char* name)
    {
        Ptr<RHI::Buffer> buffer = factory.CreateBuffer();
        buffer->SetName(ObjectName(name));
        RHI::BufferInitRequest request;
        request.m_buffer     = buffer.get();
        request.m_descriptor = descriptor;
        if (pool.InitBuffer(request) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedBuffer] Buffer {} init failed.", name);
            return nullptr;
        }
        return buffer;
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
        LOG_ERROR("[ReservedBuffer] No physical devices.");
        return 1;
    }
    RHI::DeviceDescriptor devDesc;
    devDesc.m_frameCountMax = 2;
    if (rhi->InitDevice(*devices[0], devDesc) != RHI::ResultCode::Success)
    {
        LOG_ERROR("[ReservedBuffer] InitDevice failed.");
        return 1;
    }
    auto* factory = rhi->GetRHIFactory();
    auto* device  = rhi->GetDevice();

    const uint64_t pageSize      = device->GetLimits().m_reservedPageSizeInBytes;
    const uint64_t reservedBytes = kReservedPageCount * pageSize;
    const uint32_t wordCount     = static_cast<uint32_t>(reservedBytes / sizeof(uint32_t));
    const uint32_t pageWordCount = static_cast<uint32_t>(pageSize / sizeof(uint32_t));

    // --- Shader and pipeline ---
    auto fileSystem = CreateSystem<VFSSystem>();
    fileSystem->Init();
    fileSystem->Mount("engine", ENGINE_ASSET_DIR);
    fileSystem->Mount("sandbox", SHADER_ASSET_DIR);

    auto assetManager = CreateSystem<Resource::SparkAssetManager>();
    assetManager->Init();

    auto* am = Service<Resource::AssetManager>::Get();
    Ptr<Resource::ShaderAsset> shader = am->LoadAsset<Resource::ShaderAsset>(
        Resource::AssetId::Of<Resource::ShaderAsset>("sandbox://Shader/ReservedBufferRead.hlsl"));
    if (!shader || shader->GetStatus() != Resource::AssetStatus::Ready)
    {
        LOG_ERROR("[ReservedBuffer] Failed to load ReservedBufferRead.hlsl.");
        return 1;
    }
    auto* shaderData = shader->GetShaderData();
    const auto* csBytecode = shaderData
        ? shaderData->GetStageBytecode(RHI::ShaderStage::Compute)
        : nullptr;
    if (!csBytecode)
    {
        LOG_ERROR("[ReservedBuffer] ReservedBufferRead.hlsl has no compute bytecode.");
        return 1;
    }

    Resource::ShaderInputBuildResult built = Resource::BuildShaderInputList(*shader);
    Ptr<RHI::PipelineLayoutDescriptor> layout = factory->CreatePipelineLayoutDescriptor();
    layout->AddShaderInputDescriptors(built.list);
    layout->Finalize();

    Ptr<RHI::ShaderStageFunction> csFunc = factory->CreateShaderStageFunction(RHI::ShaderStage::Compute);
    csFunc->SetByteCode(csBytecode->bytecode);
    csFunc->Finalize();

    Ptr<RHI::PipelineLibrary> pipelineLibrary = factory->CreatePipelineLibrary();
    pipelineLibrary->Init(*device, RHI::PipelineLibraryDescriptor{});

    Ptr<RHI::PipelineState> pso = factory->CreatePipelineState();
    {
        RHI::PipelineStateDescriptorForDispatch psoDesc;
        psoDesc.m_computeFunction          = csFunc;
        psoDesc.m_pipelineLayoutDescriptor = layout;
        if (pso->Init(*device, psoDesc, pipelineLibrary.get()) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedBuffer] Compute PSO init failed.");
            return 1;
        }
    }

    // --- Queue / recorder / fence ---
    Ptr<RHI::CommandQueue> queue = factory->CreateCommandQueue();
    {
        RHI::CommandQueueDescriptor desc;
        desc.m_hardwareQueueClass = RHI::HardwareQueueClass::Graphics;
        desc.m_maxFrameQueueDepth = 1;
        if (queue->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedBuffer] Command queue init failed.");
            return 1;
        }
    }

    Ptr<RHI::CommandRecorder> recorder = factory->CreateCommandRecorder();
    {
        RHI::CommandRecorderDescriptor desc;
        desc.m_queue = RHI::HardwareQueueClass::Graphics;
        if (recorder->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedBuffer] Command recorder init failed.");
            return 1;
        }
    }

    Ptr<RHI::Fence> fence = factory->CreateFence();
    if (fence->Init(*device, RHI::FenceState::Reset) != RHI::ResultCode::Success)
    {
        LOG_ERROR("[ReservedBuffer] Fence init failed.");
        return 1;
    }

    // --- Pools ---
    RHI::BufferPoolDescriptor devicePoolDesc;
    devicePoolDesc.m_bindFlags = RHI::BufferBindFlags::ShaderReadWrite | RHI::BufferBindFlags::CopyRead | RHI::BufferBindFlags::CopyWrite;
    Ptr<RHI::BufferPool> devicePool = InitPool(*factory, *device, devicePoolDesc);

    // Its buffers are placed on whole pages: a block takes the whole backing, which a larger
    // buffer would not fit and get memory of its own.
    RHI::BufferPoolDescriptor backingPoolDesc;
    backingPoolDesc.m_blockSizeInBytes       = kBackingPageCount * pageSize;
    backingPoolDesc.m_minAllocationAlignment = pageSize;
    Ptr<RHI::BufferPool> backingPool = InitPool(*factory, *device, backingPoolDesc);

    RHI::BufferPoolDescriptor uploadPoolDesc;
    uploadPoolDesc.m_heapMemoryLevel = RHI::HeapMemoryLevel::Host;
    uploadPoolDesc.m_bindFlags       = RHI::BufferBindFlags::CopyRead;
    Ptr<RHI::BufferPool> uploadPool = InitPool(*factory, *device, uploadPoolDesc);

    RHI::BufferPoolDescriptor readbackPoolDesc;
    readbackPoolDesc.m_heapMemoryLevel  = RHI::HeapMemoryLevel::Host;
    readbackPoolDesc.m_hostMemoryAccess = RHI::HostMemoryAccess::Read;
    readbackPoolDesc.m_bindFlags        = RHI::BufferBindFlags::CopyWrite;
    Ptr<RHI::BufferPool> readbackPool = InitPool(*factory, *device, readbackPoolDesc);

    if (!devicePool || !backingPool || !uploadPool || !readbackPool)
    {
        LOG_ERROR("[ReservedBuffer] Buffer pool init failed.");
        return 1;
    }

    // --- Buffers ---
    RHI::BufferDescriptor reservedDesc(RHI::BufferBindFlags::ShaderRead | RHI::BufferBindFlags::CopyWrite, reservedBytes);
    reservedDesc.m_reserved = true;
    Ptr<RHI::Buffer> reserved = InitBuffer(*factory, *devicePool, reservedDesc, "Reserved");

    Ptr<RHI::Buffer> backing = InitBuffer(*factory, *backingPool,
        RHI::BufferDescriptor(RHI::BufferBindFlags::None, kBackingPageCount * pageSize), "Backing");

    Ptr<RHI::Buffer> output = InitBuffer(*factory, *devicePool,
        RHI::BufferDescriptor(RHI::BufferBindFlags::ShaderWrite | RHI::BufferBindFlags::CopyRead, reservedBytes), "Output");

    eastl::vector<uint32_t> pattern(wordCount);
    for (uint32_t i = 0; i < wordCount; ++i)
    {
        pattern[i] = i + 1;
    }
    Ptr<RHI::Buffer> upload = factory->CreateBuffer();
    upload->SetName(ObjectName("Upload"));
    {
        RHI::BufferInitRequest request;
        request.m_buffer      = upload.get();
        request.m_descriptor  = RHI::BufferDescriptor(RHI::BufferBindFlags::CopyRead, reservedBytes);
        request.m_initialData = pattern.data();
        if (uploadPool->InitBuffer(request) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedBuffer] Buffer Upload init failed.");
            return 1;
        }
    }

    Ptr<RHI::Buffer> readback = InitBuffer(*factory, *readbackPool,
        RHI::BufferDescriptor(RHI::BufferBindFlags::CopyWrite, reservedBytes), "Readback");

    if (!reserved || !backing || !output || !readback)
    {
        return 1;
    }

    // --- Views and bindings ---
    Ptr<RHI::BufferView> reservedView = factory->CreateBufferView();
    Ptr<RHI::BufferView> outputView   = factory->CreateBufferView();
    const RHI::BufferViewDescriptor rawDesc = RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(reservedBytes));
    if (reservedView->Init(*reserved, rawDesc) != RHI::ResultCode::Success
        || outputView->Init(*output, rawDesc) != RHI::ResultCode::Success)
    {
        LOG_ERROR("[ReservedBuffer] Buffer view init failed.");
        return 1;
    }

    Ptr<RHI::ShaderBindings> bindings = factory->CreateShaderBindings();
    {
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = 0;
        if (bindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedBuffer] ShaderBindings init failed.");
            return 1;
        }
    }
    auto* reservedInput = bindings->FindBufferInput(RHI::InputName("g_Reserved"));
    auto* outputInput   = bindings->FindBufferInput(RHI::InputName("g_Output"));
    if (!reservedInput || !outputInput)
    {
        LOG_ERROR("[ReservedBuffer] Shader inputs g_Reserved / g_Output not found.");
        return 1;
    }
    reservedInput->SetView(0, reservedView.get());
    outputInput->SetView(0, outputView.get());
    factory->AcquireShaderInputCompiler(*device).Compile(*bindings);

    // --- Map the first and the last page; the middle one stays unmapped ---
    {
        RHI::BufferTileMapping mappings[2];
        mappings[0].m_buffer            = reserved.get();
        mappings[0].m_byteOffset        = 0;
        mappings[0].m_byteCount         = pageSize;
        mappings[0].m_backingBuffer     = backing.get();
        mappings[0].m_backingByteOffset = 0;
        mappings[1].m_buffer            = reserved.get();
        mappings[1].m_byteOffset        = 2 * pageSize;
        mappings[1].m_byteCount         = pageSize;
        mappings[1].m_backingBuffer     = backing.get();
        mappings[1].m_backingByteOffset = pageSize;
        if (queue->UpdateTileMappings(mappings) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedBuffer] Mapping the pages failed.");
            return 1;
        }
    }

    // --- Copy in, read through the view, copy out ---
    RHI::CommandList* cmd = recorder->GetCommandList();

    cmd->QueueBarrier(RHI::ConvertToCopyWrite(*reserved));
    cmd->FlushBarriers();
    {
        RHI::CopyBufferDescriptor copy;
        copy.m_sourceBuffer      = upload.get();
        copy.m_destinationBuffer = reserved.get();
        copy.m_size              = reservedBytes;
        cmd->Submit(RHI::CopyItem(copy));
    }

    cmd->QueueBarrier(RHI::ConvertToShaderRead(*reserved));
    cmd->QueueBarrier(RHI::ConvertToShaderWrite(*output));
    cmd->FlushBarriers();

    cmd->SetPipelineState(*pso);
    cmd->BindShaderInputsForDispatch(*bindings);
    {
        RHI::DispatchItem dispatch;
        dispatch.m_arguments = RHI::DispatchArguments(RHI::DispatchDirect(wordCount, 1, 1, kGroupSize, 1, 1));
        cmd->Submit(dispatch);
    }

    cmd->QueueBarrier(RHI::ConvertToCopyRead(*output));
    cmd->FlushBarriers();
    {
        RHI::CopyBufferDescriptor copy;
        copy.m_sourceBuffer      = output.get();
        copy.m_destinationBuffer = readback.get();
        copy.m_size              = reservedBytes;
        cmd->Submit(RHI::CopyItem(copy));
    }

    Submit(*queue, *recorder, *cmd, *fence);

    // --- Check ---
    bool passed = true;
    {
        RHI::BufferMapRequest mapRequest(*readback, 0, reservedBytes);
        RHI::BufferMapResponse mapResponse;
        if (readbackPool->MapBuffer(mapRequest, mapResponse) != RHI::ResultCode::Success || !mapResponse.m_data)
        {
            LOG_ERROR("[ReservedBuffer] Readback map failed.");
            return 1;
        }
        const auto* words = static_cast<const uint32_t*>(mapResponse.m_data);
        for (uint32_t page = 0; page < kReservedPageCount; ++page)
        {
            const bool mapped = page != 1;
            uint32_t   wrong  = 0;
            for (uint32_t i = page * pageWordCount; i < (page + 1) * pageWordCount; ++i)
            {
                const uint32_t expected = mapped ? pattern[i] : 0;
                if (words[i] != expected)
                {
                    ++wrong;
                }
            }
            if (wrong != 0)
            {
                passed = false;
                LOG_ERROR("[ReservedBuffer] Page {} ({}): {} of {} words wrong.",
                          page, mapped ? "mapped" : "unmapped", wrong, pageWordCount);
            }
            else
            {
                LOG_INFO("[ReservedBuffer] Page {} ({}) reads as expected.", page, mapped ? "mapped" : "unmapped");
            }
        }
        readbackPool->UnmapBuffer(*readback);
    }

    // --- Unmap everything, then the backing can go ---
    {
        RHI::BufferTileMapping unmap;
        unmap.m_buffer     = reserved.get();
        unmap.m_byteOffset = 0;
        unmap.m_byteCount  = reservedBytes;
        if (queue->UpdateTileMappings(eastl::span<const RHI::BufferTileMapping>(&unmap, 1)) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedBuffer] Unmapping the pages failed.");
            return 1;
        }
        queue->FlushCommands(*fence);
    }
    backing.reset();

    if (!passed)
    {
        return 1;
    }
    LOG_INFO("[ReservedBuffer] Done.");
    return 0;
}
