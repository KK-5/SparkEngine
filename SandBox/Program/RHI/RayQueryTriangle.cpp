// RayQueryTriangle — builds a BLAS of one triangle and a TLAS of two instances of it, then traces
// them with an inline ray query.
//
// The BLAS and the TLAS sit in one buffer, at offset 0 and the next multiple of 256. The vertices
// are copied into the Device heap, the instances written by the CPU, and the two builds share one
// scratch. Each frame a compute shader casts one orthographic ray per pixel into an image, copied
// into the back buffer: two triangles at x = -0.5 and +0.5, corners red, green and blue in vertex
// order, the left one at full brightness (instance 0) and the right one at half (instance 1).

#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <Base.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/HardwareQueue.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Bus/FrameEventBus.h>
#include <RHI/Command/CommandQueue.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/PipelineLibrary.h>
#include <RHI/Pipeline/PipelineState.h>
#include <RHI/Pipeline/PipelineStateDescriptor.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/RayTracing/AccelerationStructure.h>
#include <RHI/RayTracing/AccelerationStructureBuildInputs.h>
#include <RHI/RayTracing/RayTracingInstance.h>
#include <RHI/Resource/ResourceState.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Resource/ShaderInput/ShaderInput.h>
#include <RHI/Resource/ShaderInput/ShaderInputCompiler.h>
#include <RHI/SwapChain/SwapChain.h>
#include <RHI/Backend/DX12/RHISystem.h>

#include <Resource/AssetManager.h>
#include <VFS/VFSSystem.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderBuilder.h>

#include "../Common/SimpleGlfwWindow.h"

#include <EASTL/algorithm.h>
#include <cstdint>

using namespace Spark;

namespace
{
    constexpr int      kWidth     = 1024;
    constexpr int      kHeight    = 576;
    constexpr uint32_t kGroupSize = 8;

    struct Vertex
    {
        float m_position[3];
    };

    constexpr Vertex kVertices[3] =
    {
        {{  0.0f,   0.4f, 0.0f }},
        {{ -0.35f, -0.35f, 0.0f }},
        {{  0.35f, -0.35f, 0.0f }},
    };

    constexpr uint64_t AlignUp(uint64_t value, uint64_t alignment)
    {
        return (value + alignment - 1) / alignment * alignment;
    }

    Ptr<RHI::BufferPool> InitPool(RHI::Factory& factory, RHI::Device& device, const RHI::BufferPoolDescriptor& descriptor)
    {
        Ptr<RHI::BufferPool> pool = factory.CreateBufferPool();
        if (pool->Init(device, descriptor) != RHI::ResultCode::Success)
        {
            return nullptr;
        }
        return pool;
    }

    Ptr<RHI::Buffer> InitBuffer(RHI::Factory& factory, RHI::BufferPool& pool, const RHI::BufferDescriptor& descriptor,
                                const char* name, const void* initialData = nullptr)
    {
        Ptr<RHI::Buffer> buffer = factory.CreateBuffer();
        buffer->SetName(ObjectName(name));
        RHI::BufferInitRequest request;
        request.m_buffer      = buffer.get();
        request.m_descriptor  = descriptor;
        request.m_initialData = initialData;
        if (pool.InitBuffer(request) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RayQueryTriangle] Buffer {} init failed.", name);
            return nullptr;
        }
        return buffer;
    }

    Ptr<RHI::AccelerationStructure> InitAccelerationStructure(
        RHI::Factory& factory, RHI::Device& device, RHI::AccelerationStructureType type,
        const RHI::Buffer& buffer, uint64_t offset, uint64_t size)
    {
        Ptr<RHI::AccelerationStructure> structure = factory.CreateAccelerationStructure();
        RHI::AccelerationStructureDescriptor descriptor;
        descriptor.m_type   = type;
        descriptor.m_buffer = &buffer;
        descriptor.m_offset = offset;
        descriptor.m_size   = size;
        if (structure->Init(device, descriptor) != RHI::ResultCode::Success)
        {
            return nullptr;
        }
        return structure;
    }

    RHI::RayTracingInstance MakeInstance(float translateX, uint32_t instanceId, uint64_t blasAddress)
    {
        RHI::RayTracingInstance instance{};
        instance.m_transform[0][0] = 1.0f;
        instance.m_transform[1][1] = 1.0f;
        instance.m_transform[2][2] = 1.0f;
        instance.m_transform[0][3] = translateX;
        instance.m_instanceId      = instanceId;
        instance.m_mask            = 0xFF;
        instance.m_hitGroupOffset  = 0;
        instance.m_flags           = static_cast<uint32_t>(RHI::RayTracingInstanceFlags::None);
        instance.m_accelerationStructureAddress = blasAddress;
        return instance;
    }

    void SubmitAndWait(RHI::CommandQueue& queue, RHI::CommandList& commandList, RHI::Fence& fence)
    {
        commandList.Close();
        RHI::CommandList* lists[] = { &commandList };
        queue.ExecuteCommands(lists);
        queue.FlushCommands(fence);
    }
}

int main(int, char**)
{
    auto window = CreateSystem<SandBox::SimpleGlfwWindow>(kWidth, kHeight, "RayQueryTriangle");
    window->Init();

    LogConfig logConfig{};
    logConfig.m_showTimeStamp = true;
    UniquePtr<ILogSystem> logger = eastl::make_unique<SpdLogSystem>(logConfig);

    // --- RHI device ---
    auto rhiSystem = CreateSystem<RHI::DX12::RHISystem>();
    rhiSystem->Init();
    auto* rhi = Service<RHI::RHIInterface>::Get();
    RHI::PhysicalDeviceList devices = rhi->EnumeratePhysicalDevices();
    if (devices.empty())
    {
        LOG_ERROR("[RayQueryTriangle] No physical devices.");
        return 1;
    }
    RHI::DeviceDescriptor devDesc;
    devDesc.m_frameCountMax = 2;
    if (rhi->InitDevice(*devices[0], devDesc) != RHI::ResultCode::Success)
    {
        LOG_ERROR("[RayQueryTriangle] InitDevice failed.");
        return 1;
    }
    auto* factory = rhi->GetRHIFactory();
    auto* device  = rhi->GetDevice();

    // --- Shader and pipeline ---
    auto fileSystem = CreateSystem<VFSSystem>();
    fileSystem->Init();
    fileSystem->Mount("engine", ENGINE_ASSET_DIR);
    fileSystem->Mount("sandbox", SHADER_ASSET_DIR);

    auto assetManager = CreateSystem<Resource::SparkAssetManager>();
    assetManager->Init();

    auto* am = Service<Resource::AssetManager>::Get();
    Ptr<Resource::ShaderAsset> shader = am->LoadAsset<Resource::ShaderAsset>(
        Resource::AssetId::Of<Resource::ShaderAsset>("sandbox://Shader/RayQueryTriangle.hlsl"));
    if (!shader || shader->GetStatus() != Resource::AssetStatus::Ready)
    {
        LOG_ERROR("[RayQueryTriangle] Failed to load RayQueryTriangle.hlsl.");
        return 1;
    }
    auto* shaderData = shader->GetShaderData();
    const auto* csBytecode = shaderData
        ? shaderData->GetStageBytecode(RHI::ShaderStage::Compute)
        : nullptr;
    if (!csBytecode)
    {
        LOG_ERROR("[RayQueryTriangle] RayQueryTriangle.hlsl has no compute bytecode.");
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
            LOG_ERROR("[RayQueryTriangle] Compute PSO init failed.");
            return 1;
        }
    }

    // --- Queue / fence / swap chain ---
    Ptr<RHI::CommandQueue> queue = factory->CreateCommandQueue();
    {
        RHI::CommandQueueDescriptor desc;
        desc.m_hardwareQueueClass = RHI::HardwareQueueClass::Graphics;
        desc.m_maxFrameQueueDepth = 1;
        if (queue->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RayQueryTriangle] Command queue init failed.");
            return 1;
        }
    }

    Ptr<RHI::Fence> fence = factory->CreateFence();
    if (fence->Init(*device, RHI::FenceState::Reset) != RHI::ResultCode::Success)
    {
        LOG_ERROR("[RayQueryTriangle] Fence init failed.");
        return 1;
    }

    const Math::Vector2Int windowSize = window->GetWindowSize();
    const uint32_t width  = static_cast<uint32_t>(windowSize.x);
    const uint32_t height = static_cast<uint32_t>(windowSize.y);

    Ptr<RHI::SwapChain> swapChain = factory->CreateSwapChain();
    {
        RHI::SwapChainDescriptor desc;
        desc.m_dimensions.m_imageCount  = 2;
        desc.m_dimensions.m_imageFormat = RHI::Format::R8G8B8A8_UNORM;
        desc.m_dimensions.m_imageWidth  = width;
        desc.m_dimensions.m_imageHeight = height;
        desc.m_window = window->GetNativeHandle();
        if (swapChain->Init(*device, *queue, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RayQueryTriangle] Swap chain init failed.");
            return 1;
        }
    }

    // --- Pools ---
    RHI::BufferPoolDescriptor uploadPoolDesc;
    uploadPoolDesc.m_heapMemoryLevel = RHI::HeapMemoryLevel::Host;
    uploadPoolDesc.m_bindFlags       = RHI::BufferBindFlags::CopyRead | RHI::BufferBindFlags::RayTracingBuildInput;
    Ptr<RHI::BufferPool> uploadPool = InitPool(*factory, *device, uploadPoolDesc);

    RHI::BufferPoolDescriptor inputPoolDesc;
    inputPoolDesc.m_bindFlags = RHI::BufferBindFlags::CopyWrite | RHI::BufferBindFlags::RayTracingBuildInput;
    Ptr<RHI::BufferPool> inputPool = InitPool(*factory, *device, inputPoolDesc);

    RHI::BufferPoolDescriptor storagePoolDesc;
    storagePoolDesc.m_bindFlags = RHI::BufferBindFlags::RayTracingAccelerationStructure;
    Ptr<RHI::BufferPool> storagePool = InitPool(*factory, *device, storagePoolDesc);

    RHI::BufferPoolDescriptor scratchPoolDesc;
    scratchPoolDesc.m_bindFlags = RHI::BufferBindFlags::RayTracingScratchBuffer;
    Ptr<RHI::BufferPool> scratchPool = InitPool(*factory, *device, scratchPoolDesc);

    if (!uploadPool || !inputPool || !storagePool || !scratchPool)
    {
        LOG_ERROR("[RayQueryTriangle] Buffer pool init failed.");
        return 1;
    }

    Ptr<RHI::ImagePool> imagePool = factory->CreateImagePool();
    {
        RHI::ImagePoolDescriptor desc;
        desc.m_bindFlags = RHI::ImageBindFlags::ShaderWrite | RHI::ImageBindFlags::CopyRead;
        if (imagePool->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RayQueryTriangle] Image pool init failed.");
            return 1;
        }
    }

    // --- Build inputs and sizes ---
    Ptr<RHI::Buffer> vertexUpload = InitBuffer(*factory, *uploadPool,
        RHI::BufferDescriptor(RHI::BufferBindFlags::CopyRead, sizeof(kVertices)), "VertexUpload", kVertices);
    Ptr<RHI::Buffer> vertices = InitBuffer(*factory, *inputPool,
        RHI::BufferDescriptor(RHI::BufferBindFlags::CopyWrite | RHI::BufferBindFlags::RayTracingBuildInput, sizeof(kVertices)),
        "Vertices");
    if (!vertexUpload || !vertices)
    {
        return 1;
    }

    RHI::RayTracingGeometry geometry;
    geometry.m_vertexBuffer = vertices.get();
    geometry.m_vertexOffset = 0;
    geometry.m_vertexStride = sizeof(Vertex);
    geometry.m_vertexFormat = RHI::Format::R32G32B32_FLOAT;
    geometry.m_vertexCount  = 3;
    geometry.m_opaque       = true;

    RHI::AccelerationStructureBuildInputs blasInputs;
    blasInputs.m_type       = RHI::AccelerationStructureType::BottomLevel;
    blasInputs.m_flags      = RHI::AccelerationStructureBuildFlags::PreferFastTrace;
    blasInputs.m_geometries = eastl::span<const RHI::RayTracingGeometry>(&geometry, 1);

    constexpr uint32_t kInstanceCount = 2;
    RHI::AccelerationStructureBuildInputs tlasInputs;
    tlasInputs.m_type          = RHI::AccelerationStructureType::TopLevel;
    tlasInputs.m_flags         = RHI::AccelerationStructureBuildFlags::PreferFastTrace;
    tlasInputs.m_instanceCount = kInstanceCount;

    const RHI::AccelerationStructureBuildSizes blasSizes = device->GetAccelerationStructureBuildSizes(blasInputs);
    const RHI::AccelerationStructureBuildSizes tlasSizes = device->GetAccelerationStructureBuildSizes(tlasInputs);
    if (blasSizes.m_resultSize == 0 || tlasSizes.m_resultSize == 0)
    {
        LOG_ERROR("[RayQueryTriangle] The size query failed.");
        return 1;
    }
    LOG_INFO("[RayQueryTriangle] BLAS {} bytes, scratch {}; TLAS {} bytes, scratch {}.",
             blasSizes.m_resultSize, blasSizes.m_buildScratchSize, tlasSizes.m_resultSize, tlasSizes.m_buildScratchSize);

    // --- Storage, scratch, structures, instances ---
    const uint64_t tlasOffset = AlignUp(blasSizes.m_resultSize, RHI::Alignment::AccelerationStructure);
    Ptr<RHI::Buffer> storage = InitBuffer(*factory, *storagePool,
        RHI::BufferDescriptor(RHI::BufferBindFlags::RayTracingAccelerationStructure, tlasOffset + tlasSizes.m_resultSize),
        "AccelerationStructures");
    Ptr<RHI::Buffer> scratch = InitBuffer(*factory, *scratchPool,
        RHI::BufferDescriptor(RHI::BufferBindFlags::RayTracingScratchBuffer,
                              eastl::max(blasSizes.m_buildScratchSize, tlasSizes.m_buildScratchSize)),
        "Scratch");
    if (!storage || !scratch)
    {
        return 1;
    }

    Ptr<RHI::AccelerationStructure> blas = InitAccelerationStructure(*factory, *device,
        RHI::AccelerationStructureType::BottomLevel, *storage, 0, blasSizes.m_resultSize);
    Ptr<RHI::AccelerationStructure> tlas = InitAccelerationStructure(*factory, *device,
        RHI::AccelerationStructureType::TopLevel, *storage, tlasOffset, tlasSizes.m_resultSize);
    if (!blas || !tlas)
    {
        LOG_ERROR("[RayQueryTriangle] Acceleration structure init failed.");
        return 1;
    }

    const RHI::RayTracingInstance instanceData[kInstanceCount] =
    {
        MakeInstance(-0.5f, 0, blas->GetDeviceAddress()),
        MakeInstance( 0.5f, 1, blas->GetDeviceAddress()),
    };
    Ptr<RHI::Buffer> instances = InitBuffer(*factory, *uploadPool,
        RHI::BufferDescriptor(RHI::BufferBindFlags::RayTracingBuildInput, sizeof(instanceData)), "Instances", instanceData);
    if (!instances)
    {
        return 1;
    }
    tlasInputs.m_instanceBuffer = instances.get();
    tlasInputs.m_instanceOffset = 0;

    // --- Output image, bindings ---
    Ptr<RHI::Image> output = factory->CreateImage();
    {
        RHI::ImageInitRequest request;
        request.m_image      = output.get();
        request.m_descriptor = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::ShaderWrite | RHI::ImageBindFlags::CopyRead, width, height, RHI::Format::R8G8B8A8_UNORM);
        request.m_descriptor.m_mipLevels = 1;
        if (imagePool->InitImage(request) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RayQueryTriangle] Output image init failed.");
            return 1;
        }
    }

    Ptr<RHI::ImageView> outputView = factory->CreateImageView();
    {
        RHI::ImageViewDescriptor viewDesc;
        viewDesc.m_mipSliceMin   = 0;
        viewDesc.m_mipSliceMax   = 0;
        viewDesc.m_arraySliceMin = 0;
        viewDesc.m_arraySliceMax = 0;
        if (outputView->Init(*output, viewDesc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RayQueryTriangle] Output view init failed.");
            return 1;
        }
    }

    Ptr<RHI::ShaderBindings> bindings = factory->CreateShaderBindings();
    {
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = 0;
        if (bindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RayQueryTriangle] ShaderBindings init failed.");
            return 1;
        }
    }
    auto* sceneInput  = bindings->FindAccelerationStructureInput(RHI::InputName("g_Scene"));
    auto* outputInput = bindings->FindImageInput(RHI::InputName("g_Output"));
    if (!sceneInput || !outputInput)
    {
        LOG_ERROR("[RayQueryTriangle] Shader inputs g_Scene / g_Output not found.");
        return 1;
    }
    sceneInput->SetAccelerationStructure(0, tlas.get());
    outputInput->SetView(0, outputView.get());
    factory->AcquireShaderInputCompiler(*device).Compile(*bindings);

    // --- Build ---
    {
        constexpr RHI::AttachmentStage build = RHI::AttachmentStage::AccelerationStructureBuild;

        RHI::CommandList* cmd = factory->CreateCommandList(*device, RHI::HardwareQueueClass::Graphics);
        cmd->Open();

        cmd->QueueBarrier(RHI::ConvertToCopyWrite(*vertices));
        cmd->FlushBarriers();
        {
            RHI::CopyBufferDescriptor copy;
            copy.m_sourceBuffer      = vertexUpload.get();
            copy.m_destinationBuffer = vertices.get();
            copy.m_size              = sizeof(kVertices);
            cmd->Submit(RHI::CopyItem(copy));
        }

        cmd->QueueBarrier(RHI::MakeBufferBarrier(*vertices, RHI::AccessFlags::AccelStructBuildInputRead, build));
        cmd->QueueBarrier(RHI::MakeBufferBarrier(*storage, RHI::AccessFlags::AccelStructWrite, build));
        cmd->QueueBarrier(RHI::MakeBufferBarrier(*scratch, RHI::AccessFlags::AccelStructScratch, build));
        cmd->FlushBarriers();
        {
            RHI::AccelerationStructureBuildItem item;
            item.m_inputs        = blasInputs;
            item.m_destination   = blas.get();
            item.m_scratchBuffer = scratch.get();
            cmd->Submit(item);
        }

        // The TLAS build reads the BLAS and writes the TLAS, both in the storage buffer, and
        // writes the scratch the BLAS build wrote.
        cmd->QueueBarrier(RHI::MakeBufferBarrier(*storage,
            RHI::AccessFlags::AccelStructRead | RHI::AccessFlags::AccelStructWrite, build));
        cmd->QueueBarrier(RHI::MakeBufferBarrier(*scratch, RHI::AccessFlags::AccelStructScratch, build));
        cmd->QueueBarrier(RHI::MakeBufferBarrier(*instances, RHI::AccessFlags::AccelStructBuildInputRead, build));
        cmd->FlushBarriers();
        {
            RHI::AccelerationStructureBuildItem item;
            item.m_inputs        = tlasInputs;
            item.m_destination   = tlas.get();
            item.m_scratchBuffer = scratch.get();
            cmd->Submit(item);
        }

        cmd->QueueBarrier(RHI::MakeBufferBarrier(*storage, RHI::AccessFlags::AccelStructRead, RHI::AttachmentStage::ComputeShader));
        cmd->FlushBarriers();

        SubmitAndWait(*queue, *cmd, *fence);
        LOG_INFO("[RayQueryTriangle] Built.");
    }

    // --- Trace each frame ---
    while (!window->ShouldClose())
    {
        window->PollEvents();
        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameBegin);

        RHI::CommandList* cmd = factory->CreateCommandList(*device, RHI::HardwareQueueClass::Graphics);
        cmd->Open();

        cmd->QueueBarrier(RHI::MakeImageBarrier(*output, RHI::AccessFlags::ShaderStorageWrite, RHI::AttachmentStage::ComputeShader));
        cmd->FlushBarriers();

        cmd->SetPipelineState(*pso);
        cmd->BindShaderInputsForDispatch(*bindings);
        {
            RHI::DispatchItem dispatch;
            dispatch.m_arguments = RHI::DispatchArguments(RHI::DispatchDirect(width, height, 1, kGroupSize, kGroupSize, 1));
            cmd->Submit(dispatch);
        }

        RHI::Image* backBuffer = swapChain->GetCurrentImage();
        cmd->QueueBarrier(RHI::MakeImageBarrier(*output, RHI::AccessFlags::TransferRead, RHI::AttachmentStage::Copy));
        cmd->QueueBarrier(RHI::MakeImageBarrier(*backBuffer, RHI::AccessFlags::TransferWrite, RHI::AttachmentStage::Copy));
        cmd->FlushBarriers();
        {
            RHI::CopyImageDescriptor copy;
            copy.m_sourceImage      = output.get();
            copy.m_sourceSize       = RHI::Size(width, height, 1);
            copy.m_destinationImage = backBuffer;
            cmd->Submit(RHI::CopyItem(copy));
        }

        cmd->QueueBarrier(RHI::ConvertToPresent(*backBuffer));
        cmd->FlushBarriers();

        SubmitAndWait(*queue, *cmd, *fence);
        swapChain->Present();

        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameEnd);
    }

    // The last present is still queued: nothing it uses may go before it is done.
    queue->WaitForIdle();

    LOG_INFO("[RayQueryTriangle] Done.");
    return 0;
}
