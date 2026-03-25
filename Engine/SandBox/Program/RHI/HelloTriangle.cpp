#include <EASTL/unique_ptr.h>

#include <Log/SpdLogSystem.h>

#include <RHI/RHIInterface.h>
#include <RHI/Resource/ShaderResource/InputStreamLayoutBuilder.h>
#include <RHI/Attachment/RenderAttachmentLayoutBuilder.h>

#include <RHI/Backend/DX12/RHISystem.h>

#include <Resource/Asset.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/AssetManager.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderAssetCompiler.h>
#include <Resource/Common/CommonAssetLoader.h>


namespace Spark::SandBox
{
    class HelloTriangle
    {
    public:
        HelloTriangle();
        ~HelloTriangle();

        void Init();

    private:
        void CreateDevice();
        void CreateCommandQueue();
        void CreateFence();
        void CreateSwapChain();
        void CreatePipelineLibrary();
        void CreatePipelineState();

        Ptr<RHI::Device> m_device;
        Ptr<RHI::CommandQueue> m_commandQueue;
        Ptr<RHI::Fence> m_fence;
        Ptr<RHI::SwapChain> m_swapChain;
        Ptr<RHI::PipelineLibrary> m_pipelineLibrary;
        Ptr<RHI::PipelineState> m_pipelineState;

        RHI::Factory* m_rhiFactory;

        eastl::unique_ptr<ILogSystem<SpdLogSystem>> m_logger;
        eastl::unique_ptr<Spark::RHI::RHIInterface> m_rhi;
        eastl::unique_ptr<Spark::Resource::SparkAssetManager> m_assetManager;

    };


    HelloTriangle::HelloTriangle()
    {
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

        eastl::vector<eastl::string> searchPaths;
        searchPaths.push_back(SHADER_ASSET_DIR);
        m_assetManager = eastl::make_unique<Spark::Resource::SparkAssetManager>();
        m_assetManager->Initialize();
        m_assetManager->AddSearchPath(SHADER_ASSET_DIR);
        auto loader = eastl::make_unique<Resource::BinaryAssetLoader>(searchPaths);
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
        if (result == RHI::ResultCode::Success)
        {
            LOG_INFO("Create Device successed!");
        }
    }

    void HelloTriangle::CreateCommandQueue()
    {
        m_commandQueue = m_rhiFactory->CreateCommandQueue();
        RHI::CommandQueueDescriptor desc;
        desc.m_hardwareQueueClass = RHI::HardwareQueueClass::Graphics;
        desc.m_maxFrameQueueDepth = 1;
        RHI::ResultCode result = m_commandQueue->Init(*m_device, desc);
        if (result == RHI::ResultCode::Success)
        {
            LOG_INFO("Create command queue successed!");
        }
    }

    void HelloTriangle::CreateFence()
    {
        m_fence = m_rhiFactory->CreateFence();
        RHI::ResultCode result = m_fence->Init(*m_device, RHI::FenceState::Reset);
        if (result == RHI::ResultCode::Success)
        {
            LOG_INFO("Create fence successed!");
        }
    }

    void HelloTriangle::CreateSwapChain()
    {
        m_swapChain = m_rhiFactory->CreateSwapChain();
        RHI::SwapChainDescriptor desc;
        desc.m_dimensions.m_imageCount = 1;
        desc.m_dimensions.m_imageFormat = RHI::Format::R32G32B32_UINT;
        desc.m_dimensions.m_imageHeight = 1024;
        desc.m_dimensions.m_imageWidth = 576;
        desc.m_window = 0;
        RHI::ResultCode result = m_swapChain->Init(*m_device, *m_commandQueue, desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("Create swap chain failed!");
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
        builder.AddBuffer()->Channel("Postion", 0, RHI::Format::R32G32B32_FLOAT)
                           ->Channel("Color", 0, RHI::Format::R32G32B32_FLOAT);
        desc.m_inputStreamLayout = builder.End();

        // render config
        RHI::RenderAttachmentLayoutBuilder attachmentBuilder;
        attachmentBuilder.AddSubpass()->RenderTargetAttachment(RHI::Format::R32G32B32_FLOAT);
        RHI::RenderAttachmentLayout renderAttachmentLayout;
        attachmentBuilder.End(renderAttachmentLayout);
        desc.m_renderAttachmentConfiguration.m_renderAttachmentLayout = renderAttachmentLayout;
        desc.m_renderAttachmentConfiguration.m_subpassIndex = 0;

        // render state
        desc.m_renderStates = RHI::RenderStates();

        // shader
        Resource::AssetId shaderId("Shaders/Test/SimpleTriangle.hlsl");
        auto assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "Asset Manager is Null.");
        Ptr<Resource::ShaderAsset> shader = assetManager->LoadAsset(shaderId, Resource::AssetType::Shader);
        if (shader->GetStatus() != Resource::AssetStatus::Ready)
        {
            LOG_ERROR("Load shader asset failed.");
            return;
        }
        desc.m_vertexFunction = shader->GetStageBytecode(RHI::ShaderStage::Vertex);
        

    }

    void HelloTriangle::Init()
    {
        CreateDevice();
        CreateCommandQueue();
        CreateFence();
        // CreateSwapChain();
    }


}


int main(int argc, char **argv)
{
    using namespace Spark;

    Spark::SandBox::HelloTriangle app;

    app.Init();

    return 0;
}
