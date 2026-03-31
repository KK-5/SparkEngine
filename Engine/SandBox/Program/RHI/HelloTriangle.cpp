#include <EASTL/unique_ptr.h>

#include <Log/SpdLogSystem.h>
#include <Math/Vector3.h>
#include <Math/Color.h>

#include <RHI/RHIInterface.h>
#include <RHI/Resource/ShaderResource/InputStreamLayoutBuilder.h>
#include <RHI/Attachment/RenderAttachmentLayoutBuilder.h>
#include <RHI/RHILimits.h>

#include <RHI/Backend/DX12/RHISystem.h>
#include <RHI/Bus/FrameEventBus.h>

#include <Resource/Asset.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/AssetManager.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderAssetCompiler.h>
#include <Resource/Common/CommonAssetLoader.h>

#include "../Common/SimpleGlfwWindow.h"


namespace Spark::SandBox
{

    struct Vertex
    {
        Math::Vector3 position;
        Math::Color   color;
    };
    

    class HelloTriangle
    {
    public:
        HelloTriangle();
        ~HelloTriangle();

        void Init();

        void Run();

    private:
        void CreateDevice();
        void CreateCommandQueue();
        void CreateFence();
        void CreateSwapChain();
        void CreatePipelineLibrary();
        void CreatePipelineState();
        void CreateVertexBuffer();
        void CreateStageBuffer();
        void CreateViewportAndScissor();
        void BuildCommand(RHI::CommandList* commandList);

        Ptr<RHI::Device> m_device;
        Ptr<RHI::CommandQueue> m_commandQueue;
        Ptr<RHI::Fence> m_fence;
        Ptr<RHI::SwapChain> m_swapChain;
        Ptr<RHI::PipelineLibrary> m_pipelineLibrary;
        Ptr<RHI::PipelineState> m_pipelineState;

        Ptr<RHI::BufferPool> m_bufferPool;
        Ptr<RHI::Buffer> m_vertexBuffer;
        Ptr<RHI::BufferPool> m_stageBufferPool;
        Ptr<RHI::Buffer> m_stageBuffer;

        Ptr<RHI::ImageView> m_swapChainImageView;
        RHI::Image* m_swapChainCurImage;

        RHI::Viewport m_viewport;
        RHI::Scissor m_scissor;

        RHI::Factory* m_rhiFactory;

        eastl::unique_ptr<ILogSystem<SpdLogSystem>> m_logger;
        eastl::unique_ptr<Spark::RHI::RHIInterface> m_rhi;
        eastl::unique_ptr<Spark::Resource::SparkAssetManager> m_assetManager;

        eastl::unique_ptr<SimpleGlfwWindow> m_glfwWindow;

    };


    const static Vertex vert[3] =
    {
        {{ 0.0f, -0.5f, 0.f}, {1.f, 0.f, 0.f, 1.f}},
        {{ 0.5f,  0.5f, 0.f}, {0.f, 1.f, 0.f, 1.f}},
        {{-0.5f,  0.5f, 0.f}, {0.f, 0.f, 1.f, 1.f}}
    };

    HelloTriangle::HelloTriangle()
    {
        m_glfwWindow = eastl::make_unique<SimpleGlfwWindow>(1024, 576, "HelloTriangle");
        m_glfwWindow->Initialize();

        LogConfig logConfig{};
        logConfig.m_showTimeStamp = true;
        m_logger = eastl::make_unique<SpdLogSystem>(logConfig);

        m_rhi = eastl::make_unique<Spark::RHI::DX12::RHISystem>();
        m_rhi->Initialize();
        m_rhiFactory = Service<Spark::RHI::RHIInterface>::Get()->GetRHIFactory();
        if (!m_rhiFactory)
        {
            LOG_ERROR("Get RHI Factory failed");
        }

        m_assetManager = eastl::make_unique<Spark::Resource::SparkAssetManager>();
        m_assetManager->Initialize();
        m_assetManager->AddSearchPath(SHADER_ASSET_DIR);
        auto loader = eastl::make_unique<Resource::BinaryAssetLoader>();
        auto compiler = eastl::make_unique<Resource::ShaderAssetCompiler>(Resource::ShaderBackend::DXIL);
        compiler->AddStageEntry({RHI::ShaderStage::Vertex, "VSMain", "vs_6_0"});
        compiler->AddStageEntry({RHI::ShaderStage::Fragment, "PSMain", "ps_6_0"});
        m_assetManager->RegisterAssetLoader(eastl::move(loader), Resource::AssetType::Shader);
        m_assetManager->RegisterAssetCompiler(eastl::move(compiler), Resource::AssetType::Shader);

    }

    HelloTriangle::~HelloTriangle()
    {
        m_rhi->FactoryCollect();
        m_rhi->Shutdown();
        m_assetManager->Shutdown();
        m_glfwWindow->Shutdown();
    }

    void HelloTriangle::CreateDevice()
    {
        RHI::PhysicalDeviceList devList = m_rhiFactory->EnumeratePhysicalDevices();
        for (Ptr<RHI::PhysicalDevice> physicalDev: devList)
        {
            LOG_INFO(physicalDev->GetDescriptor().m_description.c_str());
        }

        m_device = m_rhiFactory->CreateDevice();
        RHI::DeviceDescriptor desc;
        desc.m_frameCountMax = 1;
        RHI::ResultCode result = m_device->Init(*devList[0], desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_INFO("Create Device failed!");
        }
    }

    void HelloTriangle::CreateCommandQueue()
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

    void HelloTriangle::CreateFence()
    {
        m_fence = m_rhiFactory->CreateFence();
        RHI::ResultCode result = m_fence->Init(*m_device, RHI::FenceState::Reset);
        if (result != RHI::ResultCode::Success)
        {
            LOG_INFO("Create fence failed!");
        }
    }

    void HelloTriangle::CreateSwapChain()
    {
        m_swapChain = m_rhiFactory->CreateSwapChain();
        RHI::SwapChainDescriptor desc;
        desc.m_dimensions.m_imageCount = 1;
        desc.m_dimensions.m_imageFormat = RHI::Format::R8G8B8A8_UNORM;
        auto windowSize = m_glfwWindow->GetWindowSize();
        desc.m_dimensions.m_imageHeight = windowSize.second;
        desc.m_dimensions.m_imageWidth = windowSize.first;
        desc.m_window = m_glfwWindow->GetNativeHandle();
        RHI::ResultCode result = m_swapChain->Init(*m_device, *m_commandQueue, desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("Create swap chain failed!");
        }

        m_swapChainCurImage = m_swapChain->GetCurrentImage();
        m_swapChainImageView = m_rhiFactory->CreateImageView();
        RHI::ImageViewDescriptor viewDesc;
        viewDesc.m_mipSliceMin = 0;
        viewDesc.m_mipSliceMax = 0;
        viewDesc.m_arraySliceMin = 0;
        viewDesc.m_arraySliceMax = 0;
        result = m_swapChainImageView->Init(*m_swapChainCurImage, viewDesc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("Create render target view failed!");
        }
    }

    void HelloTriangle::CreatePipelineLibrary()
    {
        m_pipelineLibrary = m_rhiFactory->CreatePipelineLibrary();
        RHI::PipelineLibraryDescriptor desc; // now is empty

        RHI::ResultCode result = m_pipelineLibrary->Init(*m_device, desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("Create pipeline library failed!");
        }
    }

    void HelloTriangle::CreatePipelineState()
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

        // render config. just for vulkan
        RHI::RenderAttachmentLayoutBuilder attachmentBuilder;
        attachmentBuilder.AddSubpass()->RenderTargetAttachment(RHI::Format::R8G8B8A8_UNORM);
        RHI::RenderAttachmentLayout renderAttachmentLayout;
        attachmentBuilder.End(renderAttachmentLayout);
        desc.m_renderAttachmentConfiguration.m_renderAttachmentLayout = renderAttachmentLayout;
        desc.m_renderAttachmentConfiguration.m_subpassIndex = 0;

        // render state
        desc.m_renderStates = RHI::RenderStates();
        desc.m_renderStates.m_depthStencilState.m_depth.m_enable = false;
        desc.m_renderStates.m_depthStencilState.m_stencil.m_enable = false;

        // shader
        Resource::AssetId shaderId("Shaders/Test/SimpleTriangle.hlsl");
        auto assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "Asset Manager is Null.");
        Ptr<Resource::Asset> assetBase = assetManager->LoadAsset(shaderId, Resource::AssetType::Shader);
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

    void HelloTriangle::CreateVertexBuffer()
    {
        m_bufferPool = m_rhiFactory->CreateBufferPool();
        RHI::BufferPoolDescriptor desc;
        desc.m_heapMemoryLevel = RHI::HeapMemoryLevel::Device;
        desc.m_bindFlags = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::All;
        RHI::ResultCode res = m_bufferPool->Init(*m_device, desc);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init vertex buffer pool falied");
            return;
        }

        m_vertexBuffer = m_rhiFactory->CreateBuffer();
        RHI::BufferDescriptor bufferDesc;
        bufferDesc.m_bindFlags = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;
        bufferDesc.m_byteCount = sizeof(vert);
        
        RHI::BufferInitRequest initResquest;
        initResquest.m_buffer = m_vertexBuffer.get();
        initResquest.m_descriptor = bufferDesc;
        res = m_bufferPool->InitBuffer(initResquest);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init vertex buffer falied");
        }
    }

    void HelloTriangle::CreateStageBuffer()
    {
        m_stageBufferPool = m_rhiFactory->CreateBufferPool();
        RHI::BufferPoolDescriptor desc;
        desc.m_heapMemoryLevel = RHI::HeapMemoryLevel::Host;
        desc.m_hostMemoryAccess = RHI::HostMemoryAccess::Write;
        desc.m_bindFlags = RHI::BufferBindFlags::CopyRead | RHI::BufferBindFlags::Constant;
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::All;
        RHI::ResultCode res = m_stageBufferPool->Init(*m_device, desc);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init stage buffer pool failed");
            return;
        }

        m_stageBuffer = m_rhiFactory->CreateBuffer();
        RHI::BufferDescriptor bufferDesc;
        bufferDesc.m_bindFlags = RHI::BufferBindFlags::CopyRead | RHI::BufferBindFlags::Constant;
        bufferDesc.m_byteCount = sizeof(vert);
        bufferDesc.m_alignment = RHI::Alignment::Constant;

        RHI::BufferInitRequest initRequest;
        initRequest.m_buffer = m_stageBuffer.get();
        initRequest.m_descriptor = bufferDesc;
        initRequest.m_initialData = vert;
        res = m_stageBufferPool->InitBuffer(initRequest);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init stage buffer failed");
        }
    }

    void HelloTriangle::CreateViewportAndScissor()
    {
        auto windowSize = m_glfwWindow->GetWindowSize();
        m_viewport = RHI::Viewport(0.f, (float)windowSize.first, 0.f, (float)windowSize.second);
        m_scissor = RHI::Scissor(0.f, (float)windowSize.first, 0.f, (float)windowSize.second);
    }

    void HelloTriangle::BuildCommand(RHI::CommandList* commandList)
    {
        commandList->Open();
        commandList->SetViewport(m_viewport);
        commandList->SetScissor(m_scissor);

        RHI::BufferBarrier bufferBarrier = RHI::ConvertToCopyRead(*m_stageBuffer);
        commandList->QueueBarrier(bufferBarrier);

        RHI::BufferBarrier vertexBufferBarrier = RHI::ConvertToCopyWrite(*m_vertexBuffer);
        commandList->QueueBarrier(vertexBufferBarrier);

        commandList->FlushBarriers();

        RHI::CopyItem copyItem;
        copyItem.m_type = RHI::CopyItemType::Buffer;
        copyItem.m_buffer.m_sourceBuffer = m_stageBuffer.get();
        copyItem.m_buffer.m_sourceOffset = 0;
        copyItem.m_buffer.m_destinationBuffer = m_vertexBuffer.get();
        copyItem.m_buffer.m_destinationOffset = 0;
        copyItem.m_buffer.m_size = AlignUp(
            m_vertexBuffer->GetDescriptor().m_byteCount, 
            m_vertexBuffer->GetDescriptor().m_alignment
        );
        
        commandList->Submit(copyItem);

        RHI::BufferBarrier inputAssemblyBarrier = RHI::ConvertToInputAssembly(*m_vertexBuffer);
        commandList->QueueBarrier(inputAssemblyBarrier);

        RHI::ImageBarrier imageBarrier = RHI::ConvertToRenderTarget(*m_swapChainCurImage);
        commandList->QueueBarrier(imageBarrier);

        commandList->FlushBarriers();

        RHI::ImageClearRequest clearRequest;
        clearRequest.m_imageView = m_swapChainImageView.get();
        clearRequest.m_clearValue = RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 1.f);
        commandList->ClearRenderTarget(clearRequest);

        const RHI::ImageView* renderTargets[] = { m_swapChainImageView.get() };
        commandList->SetRenderTargets(1, renderTargets);

        RHI::DrawItem drawItem;
        drawItem.m_drawInstanceArgs.m_instanceCount = 1;
        drawItem.m_drawInstanceArgs.m_instanceOffset = 0;

        drawItem.m_drawArguments.m_type = RHI::DrawType::Linear;
        drawItem.m_drawArguments.m_linear.m_vertexCount = 3;
        drawItem.m_drawArguments.m_linear.m_vertexOffset = 0;

        // Use default viewport and scissor
        /*
        drawItem.m_scissors = &m_scissor;
        drawItem.m_scissorsCount = 1;
        drawItem.m_viewports = &m_viewport;
        drawItem.m_viewportsCount = 1;
        */

        drawItem.m_pipelineState = m_pipelineState.get();

        RHI::StreamBufferView vertexStream(
            *m_vertexBuffer, 
            0, 
            m_vertexBuffer->GetDescriptor().m_byteCount,
            sizeof(Vertex)
        );

        drawItem.m_vertexBufferView.AddStreamBufferView(vertexStream);

        commandList->Submit(drawItem);

        RHI::ImageBarrier presentBarrier = RHI::ConvertToPresent(*m_swapChainCurImage);
        commandList->QueueBarrier(presentBarrier);

        commandList->Close();
    }

    void HelloTriangle::Init()
    {
        CreateDevice();
        CreateCommandQueue();
        CreateFence();
        CreateSwapChain();
        CreatePipelineLibrary();
        CreatePipelineState();
        CreateVertexBuffer();
        CreateStageBuffer();
        CreateViewportAndScissor();
    }

    void HelloTriangle::Run()
    {
        while (!m_glfwWindow->ShouldClose())
        {
            m_glfwWindow->PollEvents();
            RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameBegin);

            RHI::CommandList* commandList = m_rhiFactory->CreateCommandList(*m_device, RHI::HardwareQueueClass::Graphics);
            BuildCommand(commandList);
            RHI::CommandList* commandLists[] = { commandList };
            m_commandQueue->ExecuteCommand(commandLists);
            m_commandQueue->FlushCommands();

            m_swapChain->Present();

            RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameEnd);
        }
        
    }
}


int main(int argc, char **argv)
{
    using namespace Spark;

    Spark::SandBox::HelloTriangle app;

    app.Init();

    app.Run();

    return 0;
}
