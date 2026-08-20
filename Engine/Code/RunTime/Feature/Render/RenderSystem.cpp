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

#include <Drawable/GeometrySpec.h>
#include <Drawable/DrawTag.h>

#include <RHI/Command/DrawArguments.h>

#include <Feature/DepthPre/DepthPrePass.h>
#include <Feature/GBuffer/GBufferPass.h>
#include <Feature/Lighting/LightingPass.h>
#include <Feature/Shadow/ShadowPass.h>
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

        auto shadowPassCfg = ShadowPass::DefaultConfig();
        ShadowPass::SetUp(passContext, shadowPassCfg);

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

        // Render-side helpers + shared resources setup
        auto& rhiCtxForInit = *RHI::RHIExecuteContext::Current();
        m_sceneBindingSystem.Init(rhiCtxForInit);
        m_materialBindingSystem.Init(rhiCtxForInit);
        m_instanceBindingSystem.Init(rhiCtxForInit);
        m_shadowViewSystem.Init(rhiCtxForInit);

        m_meshGeometryComposer.Init(rhiCtxForInit);
        m_drawItemRouter.Init(rhiCtxForInit);

        {
            RHI::RHIHandle fsTri = rhiCtxForInit.CreateEntity();
            GeometrySpec fsSpec;
            fsSpec.m_drawArgs      = RHI::DrawArguments(RHI::DrawLinear(3, 0));
            fsSpec.m_instanceCount = 1;
            fsSpec.m_instanceData  = NoInstanceBinding{};
            rhiCtxForInit.Add<GeometrySpec>(fsTri, eastl::move(fsSpec));
            rhiCtxForInit.Add<FullScreenTriangleTag>(fsTri);
        }
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

        // The feature processors own no teardown: procedural specs are reaped by
        // DrawItemRouter (below), per-pass SRGs by ReapPassShaderBindings.

        m_drawItemRouter.Shutdown(*RHI::RHIExecuteContext::Current());
        m_meshGeometryComposer.Shutdown(*RHI::RHIExecuteContext::Current());

        m_instanceBindingSystem.Shutdown(*RHI::RHIExecuteContext::Current());
        m_materialBindingSystem.Shutdown(*RHI::RHIExecuteContext::Current());
        m_sceneBindingSystem.Shutdown(*RHI::RHIExecuteContext::Current());
        m_shadowViewSystem.Shutdown(*RHI::RHIExecuteContext::Current());
        m_cameraViewSystem.Shutdown(*RHI::RHIExecuteContext::Current());

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
        auto& rhiCtx = *RHI::RHIExecuteContext::Current();
        
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

        // Produce this frame's views, then encode all of them in one place. A view created
        // just now still gets picked up by CompileShaderInputs later in this same frame.
        m_cameraViewSystem.Update(renderSize);
        // After the camera views: a directional light's ortho box follows the main view.
        m_shadowViewSystem.Update();
        m_viewBindingSystem.Update();

        m_sceneBindingSystem.Update(frameIndex);
        // MaterialBindingSystem stays first, but only so a material's slot exists before
        // InstanceBindingSystem bakes it into InstanceData.m_materialIndex. The slot is
        // stable now, not rewritten every frame, so this is a one-time ordering need —
        // a material allocated later just falls back to slot 0 for one frame.
        m_materialBindingSystem.Update(frameIndex);
        m_instanceBindingSystem.Update(frameIndex);

        // World → GeometrySpec: find-or-create over renderable world entities.
        m_meshGeometryComposer.Update();
        // Producer-agnostic GeometrySpec → DrawItem routing: reap dead-dependency specs,
        // then route every not-yet-derived spec (world-composed or otherwise) through
        // the passes that accept it.
        m_drawItemRouter.Process();

        m_uiProcessFeature.Process();

        m_renderGraph.ExecutePipeline(passContext, frameIndex, renderSize);
        m_swapChain->Present();
    }
}
