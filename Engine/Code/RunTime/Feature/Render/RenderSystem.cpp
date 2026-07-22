#include "RenderSystem.h"

#include <Log/ILogSystem.h>

#include <RHI/SwapChain/SwapChainDescriptor.h>
#include <RHI/SwapChain/SwapChain.h>
#include <RHI/Pipeline/RenderTargetLayout.h>
#include <RHI/Pipeline/PipelineStateDescriptor.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Context/RHIContext.h>

#include <Pass/Pass.h>
#include <Pass/PassTag.h>
#include <Pass/PassContext.h>
#include <Pass/RenderPass.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>
#include <Pass/PassAccess.h>

#include <Feature/DepthPre/DepthPrePass.h>
#include <Feature/GBuffer/GBufferPass.h>
#include <Feature/Lighting/LightingPass.h>
#include <Feature/Skybox/SkyboxPass.h>
#include <Feature/Tonemap/TonemapPass.h>

#include "../Window/IWindowSystem.h"
#include "../UI/UIBaseSystem.h"

namespace Spark::Render
{
    bool RenderSystem::InitRHIData()
    {
        auto rhi = Service<RHI::RHIInterface>::Get();
        if (!rhi)
        {
            LOG_ERROR("[RenderSystem] There is no RHI service.");
            return false;
        }

        RHI::Factory* factory = rhi->GetRHIFactory();
        if (!factory)
        {
            LOG_ERROR("[RenderSystem] Get RHI factory failed.");
            return false;
        }

        // Device is initialized by the caller (main) via rhi->InitDevice() before
        // RenderSystem::Init runs. RenderSystem is a pure consumer here.
        RHI::Device* device = rhi->GetDevice();
        if (!device)
        {
            LOG_ERROR("[RenderSystem] RHI device is not initialized — call "
                      "RHIInterface::InitDevice() before RenderSystem::Init().");
            return false;
        }

        if (!m_renderGraph.Init(*device))
        {
            LOG_ERROR("[RenderSystem] RenderGraph init failed.");
            return false;
        }

        auto window = Service<Window::IWindowSystem>::Get();
        if (!window)
        {
            LOG_ERROR("[RenderSystem] There is no window service.");
            return false;
        }

        m_swapChain = factory->CreateSwapChain();
        RHI::SwapChainDescriptor desc;
        desc.m_dimensions.m_imageCount = device->GetDescriptor().m_frameCountMax;
        desc.m_dimensions.m_imageFormat = RHI::Format::R8G8B8A8_UNORM;
        auto windowSize = window->GetWindowSize();
        desc.m_dimensions.m_imageHeight = windowSize.y;
        desc.m_dimensions.m_imageWidth = windowSize.x;
        desc.m_window = window->GetNativeHandle();
        RHI::ResultCode result = m_swapChain->Init(
            *device,
            m_renderGraph.GetCommandQueue(RHI::HardwareQueueClass::Graphics),
            desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderSystem] Create swap chain failed!");
            return false;
        }

        return true;
    }

    bool RenderSystem::InitRenderUI()
    {
        RHI::ImGuiDescriptor desc;
        desc.m_rtvFormat = RHI::Format::R8G8B8A8_UNORM;
        desc.m_dsvFormat = RHI::Format::D32_FLOAT;
        auto* device = Service<RHI::RHIInterface>::Get()->GetDevice();
        m_rednerUI.Bind(*device, m_renderGraph.GetCommandQueue(RHI::HardwareQueueClass::Graphics), desc);
        return true;
    }

    void RenderSystem::SetUpDefaultPipeline()
    {
        auto& passContext = m_pipeline.GetPassContext();

        auto depthPrePassCfg = DepthPrePass::DefaultConfig();
        DepthPrePass::SetUp(passContext, depthPrePassCfg);

        // GBuffer (base pass): depth-equal against DepthPrePass's SceneDepth, writes
        // Albedo/Normal/ORM MRT for the deferred lighting pass to sample.
        auto gbufferPassCfg = GBufferPass::DefaultConfig();
        GBufferPass::SetUp(passContext, gbufferPassCfg);

        // Deferred lighting: samples the GBuffer, shades a hardcoded directional light
        // into SceneColor. Runs after GBuffer, before Skybox (which fills discarded sky).
        auto lightingPassCfg = LightingPass::DefaultConfig();
        LightingPass::SetUp(passContext, lightingPassCfg);

        // Skybox samples the baked cube into SceneColor (linear HDR) after depth pre.
        auto skyboxPassCfg = SkyboxPass::DefaultConfig();
        SkyboxPass::SetUp(passContext, skyboxPassCfg);

        // Final tonemap: samples the HDR SceneColor, Reinhard + gamma, writes the LDR
        // swap chain (which it now imports, replacing CopyFrameBufferPass). UIPass draws
        // on top afterwards. CopyFrameBufferPass is kept in the tree but no longer wired.
        auto tonemapPassCfg = TonemapPass::DefaultConfig();
        TonemapPass::SetUp(passContext, tonemapPassCfg);

        SPARK_RENDER_PASS(passContext, "UIPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .CustomPipeline()
            .Build([this](RenderGraphBuilder& builder)
            {
                ImageAttachmentBindInfo bind;
                bind.m_slot   = RHI::InputName("ColorOutput");
                bind.m_usage  = RHI::AttachmentUsage::RenderTarget;
                bind.m_view   = RHI::ImageViewDescriptor{};  // All image
                bind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                bind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.WriteImageAttachment<SPARK_PASS_TAG("UIPass")>(
                    RHI::AttachmentId("SwapChain"), bind);
            })
            .Execute([this](ExecuteWork& work, RenderGraphExecuter&)
            {
                m_rednerUI.Render(work.m_commandList);
            })
            .Finalize();

        // Processor setup
        auto& rhiCtxForInit = *RHI::RHIExecuteContext::Current();
        m_viewBindingSystem.Init(rhiCtxForInit);
        m_sceneBindingSystem.Init(rhiCtxForInit);
        m_materialBindingSystem.Init(rhiCtxForInit);
        m_instanceBindingSystem.Init(rhiCtxForInit);

        m_drawableComposer.Init(rhiCtxForInit);

        m_depthPreProcessor.Init();
        m_gbufferProcessor.Init();
        m_lightingProcessor.Init();
        m_skyboxProcessor.Init();
        m_tonemapProcessor.Init();
    }

    void RenderSystem::InitInternal()
    {
        InitRHIData();

        // ImportSwapChain materializes swap chain entities into the active RHIContext;
        // the context is owned and pushed by the RHI layer (see RHIInterface),
        // so by this point RHIExecuteContext::Current() is already valid.
        m_renderGraph.ImportSwapChain(*m_swapChain);

        InitRenderUI();

        PassExecuteContext::Push(m_pipeline.GetPassContext());

        TickBus::Handler::BusConnect();
    }

    void RenderSystem::ShutdownInternal()
    {
        TickBus::Handler::BusDisconnect();

        m_tonemapProcessor.Shutdown();
        m_skyboxProcessor.Shutdown();
        m_lightingProcessor.Shutdown();
        m_gbufferProcessor.Shutdown();
        m_depthPreProcessor.Shutdown();

        m_drawableComposer.Shutdown(*RHI::RHIExecuteContext::Current());

        m_instanceBindingSystem.Shutdown(*RHI::RHIExecuteContext::Current());
        m_materialBindingSystem.Shutdown(*RHI::RHIExecuteContext::Current());
        m_sceneBindingSystem.Shutdown(*RHI::RHIExecuteContext::Current());
        m_viewBindingSystem.Shutdown(*RHI::RHIExecuteContext::Current());

        // Per-pass SRGs have no external owner (created lazily via GetOrCreate,
        // tag-owned). Reap them here alongside the other binding entities.
        ReapPassShaderBindings(*RHI::RHIExecuteContext::Current());

        PassExecuteContext::Pop();

        // RenderGraph::Shutdown drains the GPU and destroys the swap chain
        // entities it imported into RHIContext. After that the swap chain
        // itself can be released — any in-flight Present has completed and
        // the imported ImageView entities are gone.
        m_renderGraph.Shutdown();
    }

    void RenderSystem::OnTick(float deltaTime)
    {
        auto& passContext = *PassExecuteContext::Current(); 
        
        const uint32_t frameIndex =m_swapChain->GetCurrentImageIndex();

        // Driver decides the render-output resolution and hands it to the graph,
        // which threads it to Build callbacks via the builder. Today that's the
        // window size; an editor would feed its viewport panel size here instead.
        // const Math::Vector2Int renderSize = Service<Window::IWindowSystem>::Get()->GetWindowSize();
        auto* ui = Service<UI::UIBaseSystem>::Get();
        Math::Vector2Int renderSize;
        if (ui)
        {
            renderSize = ui->GetFrameBufferSize();
        }
        else
        {
            renderSize = Math::Vector2Int(
                m_swapChain->GetDescriptor().m_dimensions.m_imageWidth,
                m_swapChain->GetDescriptor().m_dimensions.m_imageHeight
            );
        }

        m_viewBindingSystem.Update(renderSize);
        m_sceneBindingSystem.Update(frameIndex);
        // Order matters: MaterialBindingSystem writes each material's MaterialGPUSlot,
        // which InstanceBindingSystem reads to resolve InstanceData.m_materialIndex.
        m_materialBindingSystem.Update(frameIndex);
        m_instanceBindingSystem.Update(frameIndex);

        m_drawableComposer.Update();

        m_uiProcessFeature.Process();
        m_depthPreProcessor.Process(renderSize);
        m_gbufferProcessor.Process(renderSize);
        m_lightingProcessor.Process(renderSize);
        m_skyboxProcessor.Process(renderSize);
        m_tonemapProcessor.Process(renderSize);

        m_renderGraph.ExecutePipeline(passContext, frameIndex, renderSize);
        m_swapChain->Present();
    }
}
