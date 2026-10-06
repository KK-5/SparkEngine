#include <cstring>

#include <EASTL/vector.h>

#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <Math/Vector3.h>
#include <Math/Color.h>
#include <Base.h>

#include <RHI/RHIInterface.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/IndirectCommands.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Attachment/RenderAttachmentLayoutBuilder.h>
#include <RHI/RHILimits.h>

#include <RHI/Backend/DX12/RHISystem.h>
#include <RHI/Bus/FrameEventBus.h>

#include <Resource/Asset.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/AssetManager.h>
#include <VFS/VFSSystem.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderAssetCompiler.h>
#include <Resource/Common/CommonAssetLoader.h>

#include "../Common/SimpleGlfwWindow.h"


namespace Spark::SandBox
{
    // One indexed indirect call draws a 3 x 2 grid of quads, one record per quad.
    //  - kUseCountBuffer false: the call runs all six records, every quad shows.
    //  - kUseCountBuffer true:  the count buffer says kCountBufferValue, so only the
    //    first that many records run (3 = the top row).
    static constexpr bool     kUseCountBuffer   = true;
    static constexpr uint32_t kCountBufferValue = 3;

    //  - kSubAllocateBuffers true: the pool has a budget, so the four buffers are parts of one
    //    native buffer. The upload's barriers then assert in the DX12 backend: a part of a
    //    shared buffer cannot yet take a barrier around a write within a queue.
    static constexpr bool     kSubAllocateBuffers  = true;
    static constexpr uint64_t kBufferPoolBudget    = 64 * 1024;

    static constexpr uint32_t kQuadCount           = 6;
    static constexpr uint32_t kQuadColumnCount     = 3;
    static constexpr uint32_t kVertexCountPerQuad  = 4;
    static constexpr uint32_t kIndexCountPerQuad   = 6;

    struct Vertex
    {
        Math::Vector3 position;
        Math::Color   color;
    };

    class IndirectDraw
    {
    public:
        IndirectDraw();
        ~IndirectDraw() = default;

        void Init();

        void Run();

    private:
        void CreateDevice();
        void CreateCommandQueue();
        void CreateFence();
        void CreateSwapChain();
        void CreatePipelineLibrary();
        void CreatePipelineState();
        void BuildStageData();
        void CreateBuffers();
        void CreateStageBuffer();
        void CreateViewportAndScissor();
        void SubmitResources(RHI::CommandList* commandList);
        void BuildCommand(RHI::CommandList* commandList);

        // 依赖成员声明顺序管理生命周期
        UniquePtr<ILogSystem> m_logger;
        SystemUniquePtr<Spark::RHI::RHIInterface> m_rhi;
        SystemUniquePtr<Spark::VFSSystem>                   m_fileSystem;
        SystemUniquePtr<Spark::Resource::SparkAssetManager> m_assetManager;

        SystemUniquePtr<SimpleGlfwWindow> m_glfwWindow;

        RHI::Device* m_device = nullptr;
        Ptr<RHI::SwapChain> m_swapChain;
        Ptr<RHI::CommandQueue> m_commandQueue;
        Ptr<RHI::Fence> m_fence;
        Ptr<RHI::PipelineLibrary> m_pipelineLibrary;
        Ptr<RHI::PipelineState> m_pipelineState;

        // A destination buffer and the part of the stage buffer its content sits in.
        struct Upload
        {
            Ptr<RHI::Buffer> m_buffer;
            uint32_t         m_stageByteOffset = 0;
            uint32_t         m_byteCount       = 0;
        };

        Ptr<RHI::BufferPool> m_bufferPool;
        Upload m_vertices;
        Upload m_indices;
        Upload m_arguments;
        Upload m_count;

        Ptr<RHI::BufferPool> m_stageBufferPool;
        Ptr<RHI::Buffer> m_stageBuffer;
        eastl::vector<uint8_t> m_stageData;

        Ptr<RHI::ImageView> m_swapChainImageViews[2];
        RHI::Image* m_swapChainCurImage = nullptr;

        RHI::Viewport m_viewport;
        RHI::Scissor m_scissor;

        RHI::Factory* m_rhiFactory = nullptr;
    };

    IndirectDraw::IndirectDraw()
    {
        m_glfwWindow = CreateSystem<SimpleGlfwWindow>(1024, 576, "IndirectDraw");
        m_glfwWindow->Init();

        LogConfig logConfig{};
        logConfig.m_showTimeStamp = true;
        m_logger = eastl::make_unique<SpdLogSystem>(logConfig);

        m_rhi = CreateSystem<Spark::RHI::DX12::RHISystem>();
        m_rhi->Init();
        m_rhiFactory = Service<Spark::RHI::RHIInterface>::Get()->GetRHIFactory();
        if (!m_rhiFactory)
        {
            LOG_ERROR("Get RHI Factory failed");
        }

        m_fileSystem = CreateSystem<Spark::VFSSystem>();
        m_fileSystem->Init();
        m_fileSystem->Mount("sandbox", SHADER_ASSET_DIR);

        m_assetManager = CreateSystem<Spark::Resource::SparkAssetManager>();
        m_assetManager->Init();
    }

    void IndirectDraw::CreateDevice()
    {
        RHI::PhysicalDeviceList devList = m_rhi->EnumeratePhysicalDevices();
        ASSERT(devList.size() > 0, "No physical devices available.");
        for (Ptr<RHI::PhysicalDevice> physicalDev: devList)
        {
            LOG_INFO(physicalDev->GetDescriptor().m_description.c_str());
        }

        RHI::DeviceDescriptor desc;
        desc.m_frameCountMax = 2;
        RHI::ResultCode result = m_rhi->InitDevice(*devList[0], desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_INFO("Create Device failed!");
        }
        m_device = m_rhi->GetDevice();
    }

    void IndirectDraw::CreateCommandQueue()
    {
        m_commandQueue = m_rhiFactory->CreateCommandQueue();
        RHI::CommandQueueDescriptor desc;
        desc.m_hardwareQueueClass = RHI::HardwareQueueClass::Graphics;
        desc.m_maxFrameQueueDepth = 1;
        RHI::ResultCode result = m_commandQueue->Init(*m_device, desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_INFO("Create command queue failed!");
        }
    }

    void IndirectDraw::CreateFence()
    {
        m_fence = m_rhiFactory->CreateFence();
        RHI::ResultCode result = m_fence->Init(*m_device, RHI::FenceState::Reset);
        if (result != RHI::ResultCode::Success)
        {
            LOG_INFO("Create fence failed!");
        }
    }

    void IndirectDraw::CreateSwapChain()
    {
        m_swapChain = m_rhiFactory->CreateSwapChain();
        RHI::SwapChainDescriptor desc;
        desc.m_dimensions.m_imageCount = 2;
        desc.m_dimensions.m_imageFormat = RHI::Format::R8G8B8A8_UNORM;
        auto windowSize = m_glfwWindow->GetWindowSize();
        desc.m_dimensions.m_imageHeight = windowSize.y;
        desc.m_dimensions.m_imageWidth = windowSize.x;
        desc.m_window = m_glfwWindow->GetNativeHandle();
        RHI::ResultCode result = m_swapChain->Init(*m_device, *m_commandQueue, desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("Create swap chain failed!");
        }

        for (uint32_t i = 0; i < desc.m_dimensions.m_imageCount; ++i)
        {
            auto image = m_swapChain->GetImage(i);
            m_swapChainImageViews[i] = m_rhiFactory->CreateImageView();
            RHI::ImageViewDescriptor viewDesc;
            viewDesc.m_mipSliceMin = 0;
            viewDesc.m_mipSliceMax = 0;
            viewDesc.m_arraySliceMin = 0;
            viewDesc.m_arraySliceMax = 0;
            result = m_swapChainImageViews[i]->Init(*image, viewDesc);
            if (result != RHI::ResultCode::Success)
            {
                LOG_ERROR("Create render target view failed!");
            }
        }
    }

    void IndirectDraw::CreatePipelineLibrary()
    {
        m_pipelineLibrary = m_rhiFactory->CreatePipelineLibrary();
        RHI::PipelineLibraryDescriptor desc; // now is empty

        RHI::ResultCode result = m_pipelineLibrary->Init(*m_device, desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("Create pipeline library failed!");
        }
    }

    void IndirectDraw::CreatePipelineState()
    {
        m_pipelineState = m_rhiFactory->CreatePipelineState();

        RHI::PipelineStateDescriptorForDraw desc;

        // InputStreamLayout
        RHI::InputStreamLayoutBuilder builder;
        builder.Begin();
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        builder.AddBuffer()->Channel("POSITION", 0, RHI::Format::R32G32B32_FLOAT)
                           ->Channel("COLOR", 0, RHI::Format::R32G32B32A32_FLOAT);
        desc.m_inputStreamLayout = builder.End();

        RHI::RenderTargetLayout rtLayout;
        rtLayout.m_colorAttachmentCount = 1;
        rtLayout.m_colorFormats = {RHI::Format::R8G8B8A8_UNORM};
        desc.m_renderTargetLayout = rtLayout;

        // render state. Culling is off: winding is not what this sample is about.
        desc.m_renderStates = RHI::RenderStates();
        desc.m_renderStates.m_rasterState.m_cullMode = RHI::CullMode::None;
        desc.m_renderStates.m_depthStencilState.m_depth.m_enable = false;
        desc.m_renderStates.m_depthStencilState.m_stencil.m_enable = false;

        // shader
        Resource::AssetId shaderId = Resource::AssetId::Of<Resource::ShaderAsset>("sandbox://Shader/SimpleTriangle.hlsl");
        auto assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "Asset Manager is Null.");
        Ptr<Resource::Asset> assetBase = assetManager->LoadAsset(shaderId);
        if (assetBase->GetStatus() != Resource::AssetStatus::Ready)
        {
            LOG_ERROR("Load shader asset failed.");
            return;
        }
        auto shaderData = assetBase->GetData<Resource::ShaderAssetData>();
        Ptr<RHI::ShaderStageFunction> vertFunc = m_rhiFactory->CreateShaderStageFunction(RHI::ShaderStage::Vertex);
        vertFunc->SetByteCode(shaderData->GetStageBytecode(RHI::ShaderStage::Vertex)->bytecode);
        vertFunc->Finalize();
        Ptr<RHI::ShaderStageFunction> fragFunc = m_rhiFactory->CreateShaderStageFunction(RHI::ShaderStage::Fragment);
        fragFunc->SetByteCode(shaderData->GetStageBytecode(RHI::ShaderStage::Fragment)->bytecode);
        fragFunc->Finalize();
        desc.m_vertexFunction = vertFunc;
        desc.m_fragmentFunction = fragFunc;

        // pipelinelayout
        Ptr<RHI::PipelineLayoutDescriptor> layoutDesc = m_rhiFactory->CreatePipelineLayoutDescriptor();
        layoutDesc->Finalize();
        desc.m_pipelineLayoutDescriptor = layoutDesc;

        RHI::ResultCode res = m_pipelineState->Init(*m_device, desc, m_pipelineLibrary.get());
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init pso failed");
        }
    }

    void IndirectDraw::BuildStageData()
    {
        static const Math::Color colors[kQuadCount] =
        {
            Math::Color(glm::vec4(1.f, 0.f, 0.f, 1.f)), Math::Color(glm::vec4(0.f, 1.f, 0.f, 1.f)),
            Math::Color(glm::vec4(0.f, 0.f, 1.f, 1.f)), Math::Color(glm::vec4(1.f, 1.f, 0.f, 1.f)),
            Math::Color(glm::vec4(0.f, 1.f, 1.f, 1.f)), Math::Color(glm::vec4(1.f, 0.f, 1.f, 1.f)),
        };

        // Quad i sits at column i % 3, row i / 3, row 0 on top.
        Vertex   vertices[kQuadCount * kVertexCountPerQuad];
        uint16_t indices[kQuadCount * kIndexCountPerQuad];
        for (uint32_t i = 0; i < kQuadCount; ++i)
        {
            const float centerX = -0.6f + 0.6f * static_cast<float>(i % kQuadColumnCount);
            const float centerY =  0.4f - 0.8f * static_cast<float>(i / kQuadColumnCount);
            const float halfX   = 0.22f;
            const float halfY   = 0.3f;

            Vertex* v = vertices + i * kVertexCountPerQuad;
            v[0] = { { centerX - halfX, centerY - halfY, 0.f }, colors[i] };
            v[1] = { { centerX + halfX, centerY - halfY, 0.f }, colors[i] };
            v[2] = { { centerX + halfX, centerY + halfY, 0.f }, colors[i] };
            v[3] = { { centerX - halfX, centerY + halfY, 0.f }, colors[i] };

            const uint16_t base  = static_cast<uint16_t>(i * kVertexCountPerQuad);
            uint16_t*      index = indices + i * kIndexCountPerQuad;
            index[0] = base;
            index[1] = base + 1;
            index[2] = base + 2;
            index[3] = base;
            index[4] = base + 2;
            index[5] = base + 3;
        }

        // The first half reaches its quad through m_firstIndex, the second half reuses
        // quad 0's indices and reaches its own through m_vertexOffset, so a record whose
        // fields are laid out wrong shows up as a missing or misplaced quad.
        RHI::DrawIndexedIndirectCommand commands[kQuadCount];
        for (uint32_t i = 0; i < kQuadCount; ++i)
        {
            const bool byFirstIndex = i < kQuadCount / 2;

            RHI::DrawIndexedIndirectCommand& command = commands[i];
            command.m_indexCount    = kIndexCountPerQuad;
            command.m_instanceCount = 1;
            command.m_firstIndex    = byFirstIndex ? i * kIndexCountPerQuad : 0;
            command.m_vertexOffset  = byFirstIndex ? 0 : static_cast<int32_t>(i * kVertexCountPerQuad);
            command.m_firstInstance = 0;
        }

        const uint32_t count = kCountBufferValue;

        auto append = [this](Upload& upload, const void* data, size_t byteCount)
        {
            upload.m_stageByteOffset = static_cast<uint32_t>(m_stageData.size());
            upload.m_byteCount       = static_cast<uint32_t>(byteCount);
            m_stageData.resize(m_stageData.size() + byteCount);
            std::memcpy(m_stageData.data() + upload.m_stageByteOffset, data, byteCount);
        };
        append(m_vertices, vertices, sizeof(vertices));
        append(m_indices, indices, sizeof(indices));
        append(m_arguments, commands, sizeof(commands));
        append(m_count, &count, sizeof(count));
    }

    void IndirectDraw::CreateBuffers()
    {
        const RHI::BufferBindFlags geometryFlags = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;
        const RHI::BufferBindFlags indirectFlags = RHI::BufferBindFlags::Indirect | RHI::BufferBindFlags::CopyWrite;

        m_bufferPool = m_rhiFactory->CreateBufferPool();
        RHI::BufferPoolDescriptor desc;
        desc.m_heapMemoryLevel = RHI::HeapMemoryLevel::Device;
        desc.m_bindFlags = geometryFlags | indirectFlags;
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::All;
        desc.m_budgetInBytes = kSubAllocateBuffers ? kBufferPoolBudget : 0;
        RHI::ResultCode res = m_bufferPool->Init(*m_device, desc);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init buffer pool failed");
            return;
        }

        // The alignment is the element size: where a buffer starts inside a shared native
        // buffer has to be a whole number of its elements.
        auto create = [this](Upload& upload, RHI::BufferBindFlags bindFlags, uint64_t alignment, const char* what)
        {
            upload.m_buffer = m_rhiFactory->CreateBuffer();
            RHI::BufferDescriptor bufferDesc;
            bufferDesc.m_bindFlags = bindFlags;
            bufferDesc.m_byteCount = upload.m_byteCount;
            bufferDesc.m_alignment = alignment;

            RHI::BufferInitRequest initRequest;
            initRequest.m_buffer = upload.m_buffer.get();
            initRequest.m_descriptor = bufferDesc;
            if (m_bufferPool->InitBuffer(initRequest) != RHI::ResultCode::Success)
            {
                LOG_ERROR("Init {} buffer failed", what);
            }
        };
        // The count first, so that the vertices, whose size is no power of two, do not start
        // at 0 of a shared native buffer, where any alignment holds.
        create(m_count, indirectFlags, sizeof(uint32_t), "count");
        create(m_vertices, geometryFlags, sizeof(Vertex), "vertex");
        create(m_indices, geometryFlags, sizeof(uint16_t), "index");
        create(m_arguments, indirectFlags, sizeof(uint32_t), "argument");
    }

    void IndirectDraw::CreateStageBuffer()
    {
        m_stageBufferPool = m_rhiFactory->CreateBufferPool();
        RHI::BufferPoolDescriptor desc;
        desc.m_heapMemoryLevel = RHI::HeapMemoryLevel::Host;
        desc.m_hostMemoryAccess = RHI::HostMemoryAccess::Write;
        desc.m_bindFlags = RHI::BufferBindFlags::CopyRead;
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::All;
        RHI::ResultCode res = m_stageBufferPool->Init(*m_device, desc);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init stage buffer pool failed");
            return;
        }

        m_stageBuffer = m_rhiFactory->CreateBuffer();
        RHI::BufferDescriptor bufferDesc;
        bufferDesc.m_bindFlags = RHI::BufferBindFlags::CopyRead;
        bufferDesc.m_byteCount = m_stageData.size();

        RHI::BufferInitRequest initRequest;
        initRequest.m_buffer = m_stageBuffer.get();
        initRequest.m_descriptor = bufferDesc;
        initRequest.m_initialData = m_stageData.data();
        res = m_stageBufferPool->InitBuffer(initRequest);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init stage buffer failed");
        }
    }

    void IndirectDraw::CreateViewportAndScissor()
    {
        auto windowSize = m_glfwWindow->GetWindowSize();
        m_viewport = RHI::Viewport(0.f, (float)windowSize.x, 0.f, (float)windowSize.y);
        m_scissor = RHI::Scissor(0.f, 0.f, (float)windowSize.x, (float)windowSize.y);
    }

    void IndirectDraw::SubmitResources(RHI::CommandList* commandList)
    {
        Upload* uploads[] = { &m_vertices, &m_indices, &m_arguments, &m_count };

        commandList->Open();

        commandList->QueueBarrier(RHI::ConvertToCopyRead(*m_stageBuffer));
        for (Upload* upload : uploads)
        {
            commandList->QueueBarrier(RHI::ConvertToCopyWrite(*upload->m_buffer));
        }
        commandList->FlushBarriers();

        for (Upload* upload : uploads)
        {
            RHI::CopyItem copyItem;
            copyItem.m_type = RHI::CopyItemType::Buffer;
            copyItem.m_buffer.m_sourceBuffer = m_stageBuffer.get();
            copyItem.m_buffer.m_sourceOffset = upload->m_stageByteOffset;
            copyItem.m_buffer.m_destinationBuffer = upload->m_buffer.get();
            copyItem.m_buffer.m_destinationOffset = 0;
            copyItem.m_buffer.m_size = upload->m_byteCount;
            commandList->Submit(copyItem);
        }

        commandList->QueueBarrier(RHI::ConvertToInputAssembly(*m_vertices.m_buffer));
        commandList->QueueBarrier(RHI::ConvertToInputAssembly(*m_indices.m_buffer));
        commandList->QueueBarrier(RHI::ConvertToIndirect(*m_arguments.m_buffer));
        commandList->QueueBarrier(RHI::ConvertToIndirect(*m_count.m_buffer));
        commandList->FlushBarriers();

        commandList->Close();
    }

    void IndirectDraw::BuildCommand(RHI::CommandList* commandList)
    {
        commandList->Open();
        commandList->SetViewport(m_viewport);

        m_swapChainCurImage = m_swapChain->GetCurrentImage();
        RHI::ImageBarrier imageBarrier = RHI::ConvertToRenderTarget(*m_swapChainCurImage);
        commandList->QueueBarrier(imageBarrier);

        commandList->FlushBarriers();

        commandList->SetScissor(m_scissor);

        RHI::RenderPassBeginInfo beginInfo;
        beginInfo.m_colorAttachmentCount = 1;

        RHI::RenderPassColorAttachment renderTarget;
        renderTarget.m_view = m_swapChainImageViews[m_swapChain->GetCurrentImageIndex()].get();
        RHI::AttachmentLoadStoreAction loadStoreAction;
        loadStoreAction.m_clearValue = RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 1.f);
        loadStoreAction.m_loadAction = RHI::AttachmentLoadAction::Clear;
        loadStoreAction.m_storeAction = RHI::AttachmentStoreAction::Store;
        renderTarget.m_loadStoreAction = loadStoreAction;
        beginInfo.m_colorAttachments = {renderTarget};

        commandList->BeginRenderPass(beginInfo);

        commandList->SetPipelineState(*m_pipelineState);

        RHI::IndirectArguments arguments;
        arguments.m_buffer     = m_arguments.m_buffer.get();
        arguments.m_byteOffset = 0;
        arguments.m_maxCount   = kQuadCount;
        if (kUseCountBuffer)
        {
            arguments.m_countBuffer     = m_count.m_buffer.get();
            arguments.m_countByteOffset = 0;
        }

        // The instance arguments stay unused: an indirect draw takes them from its records.
        RHI::DrawItem drawItem;
        drawItem.m_drawArguments = RHI::DrawArguments(RHI::DrawIndexedIndirect(arguments));

        RHI::VertexInputView vertexStream(
            *m_vertices.m_buffer,
            0,
            m_vertices.m_byteCount,
            sizeof(Vertex)
        );
        drawItem.m_vertexBufferView.AddVertexInputView(vertexStream);

        drawItem.m_indexBufferView = RHI::IndexBufferView(
            *m_indices.m_buffer,
            0,
            m_indices.m_byteCount,
            RHI::IndexFormat::UINT16);

        commandList->Submit(drawItem);

        commandList->EndRenderPass();

        RHI::ImageBarrier presentBarrier = RHI::ConvertToPresent(*m_swapChainCurImage);
        commandList->QueueBarrier(presentBarrier);

        commandList->Close();
    }

    void IndirectDraw::Init()
    {
        CreateDevice();
        CreateCommandQueue();
        CreateFence();
        CreateSwapChain();
        CreatePipelineLibrary();
        CreatePipelineState();
        BuildStageData();
        CreateBuffers();
        CreateStageBuffer();
        CreateViewportAndScissor();
    }

    void IndirectDraw::Run()
    {
        RHI::CommandList* commandList = m_rhiFactory->CreateCommandList(*m_device, RHI::HardwareQueueClass::Graphics);
        SubmitResources(commandList);
        RHI::CommandList* commandLists[] = { commandList };
        m_commandQueue->ExecuteCommands(commandLists);
        m_commandQueue->FlushCommands(*m_fence);

        while (!m_glfwWindow->ShouldClose())
        {
            m_glfwWindow->PollEvents();
            RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameBegin);

            RHI::CommandList* commandList = m_rhiFactory->CreateCommandList(*m_device, RHI::HardwareQueueClass::Graphics);
            BuildCommand(commandList);
            RHI::CommandList* commandLists[] = { commandList };
            m_commandQueue->ExecuteCommands(commandLists);

            m_commandQueue->FlushCommands(*m_fence);

            m_swapChain->Present();

            RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameEnd);
        }
    }
}


int main(int argc, char **argv)
{
    using namespace Spark;

    Spark::SandBox::IndirectDraw app;

    app.Init();

    app.Run();

    return 0;
}
