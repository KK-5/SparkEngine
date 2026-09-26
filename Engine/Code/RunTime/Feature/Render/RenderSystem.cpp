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
#include <Feature/ShadowProjection/ShadowProjectionPass.h>
#include <Feature/Lighting/LightsPass.h>
#include <Feature/Lighting/IndirectDiffusePass.h>
#include <Feature/Lighting/ReflectionsPass.h>
#include <Feature/Shadow/ShadowPass.h>
#include <Feature/Skybox/SkyboxPass.h>
#include <Feature/Tonemap/TonemapPass.h>
#include <Feature/Velocity/VelocityResolvePass.h>
#include <Feature/TemporalAA/TemporalAAPass.h>
#include <Feature/SceneDownsample/SceneDownsamplePass.h>
#include <Feature/Bloom/BloomPass.h>
#include <Feature/UI/UIPass.h>

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
        m_requestedSwapChainSize = windowSize;

        return true;
    }

    void RenderSystem::SyncSwapChainToWindow()
    {
        auto* window = Service<Window::IWindowSystem>::Get();
        if (!window || !m_swapChain)
        {
            return;
        }

        const Math::Vector2Int windowSize = window->GetWindowSize();
        if (windowSize.x <= 0 || windowSize.y <= 0)
        {
            // Minimized: keep rendering into the swap chain as it is. Presenting to a
            // minimized window is legal, whereas skipping the frame would leave ImGui's
            // frame open — ImGui::Render only runs inside the graph, from UIPass.
            return;
        }

        if (windowSize == m_requestedSwapChainSize)
        {
            return;
        }

        if (m_renderGraph.ResizeSwapChain(*m_swapChain, windowSize))
        {
            m_requestedSwapChainSize = windowSize;
        }
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

        auto velocityResolvePassCfg = VelocityResolvePass::DefaultConfig();
        VelocityResolvePass::SetUp(passContext, velocityResolvePassCfg);

        // Resolves the shadow atlas into ShadowMask, the screen-space visibility signal
        // the lighting reads instead of sampling the atlas itself.
        auto shadowProjectionCfg = ShadowProjectionPass::DefaultConfig();
        ShadowProjectionPass::SetUp(passContext, shadowProjectionCfg);

        // Deferred lighting, one pass per signal so each can be replaced on its own -- direct
        // light, indirect diffuse (later DDGI), reflections (later SSR). All three blend
        // additively into SceneColor, so this order is only for reading. After GBuffer,
        // before Skybox (which fills the sky they depth-cull).
        auto lightsPassCfg = LightsPass::DefaultConfig();
        LightsPass::SetUp(passContext, lightsPassCfg);

        auto indirectDiffuseCfg = IndirectDiffusePass::DefaultConfig();
        IndirectDiffusePass::SetUp(passContext, indirectDiffuseCfg);

        auto reflectionsCfg = ReflectionsPass::DefaultConfig();
        ReflectionsPass::SetUp(passContext, reflectionsCfg);

        // Skybox samples the baked cube into SceneColor (linear HDR) after depth pre.
        auto skyboxPassCfg = SkyboxPass::DefaultConfig();
        SkyboxPass::SetUp(passContext, skyboxPassCfg);

        // After everything that writes SceneColor; TonemapPass reads its output when enabled.
        auto temporalAAPassCfg = TemporalAAPass::DefaultConfig();
        TemporalAAPass::SetUp(passContext, temporalAAPassCfg);

        // Halves the finished scene color level by level, for bloom and later the exposure histogram.
        SceneDownsamplePass::SetUp(passContext);

        // Upsamples that chain back into the glow TonemapPass blends in.
        BloomPass::SetUp(passContext);

        // Final tonemap: samples the HDR SceneColor, Reinhard + gamma, writes the LDR
        // swap chain, which it imports. UIPass draws on top afterwards.
        auto tonemapPassCfg = TonemapPass::DefaultConfig();
        TonemapPass::SetUp(passContext, tonemapPassCfg);

        UIPass::SetUp(passContext, m_rednerUI);

        // Render-side helpers + shared resources setup
        auto& rhiCtxForInit = *RHI::RHIExecuteContext::Current();
        m_sceneBindingSystem.Init(rhiCtxForInit);
        m_materialBindingSystem.Init(rhiCtxForInit);
        m_instanceBindingSystem.Init(rhiCtxForInit);
        m_shadowViewSystem.Init(rhiCtxForInit);
        m_shadowMaskSystem.Init(rhiCtxForInit);

        m_meshGeometryComposer.Init(rhiCtxForInit);
        m_drawItemRouter.Init(rhiCtxForInit);
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
        m_shadowMaskSystem.Shutdown(*RHI::RHIExecuteContext::Current());
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

    void RenderSystem::OnTick(const FrameTime& time)
    {
        auto& passContext = *PassExecuteContext::Current();
        auto& rhiCtx = *RHI::RHIExecuteContext::Current();

        // Must stay ahead of frameIndex: a resize rewinds the current image index to 0.
        SyncSwapChainToWindow();

        const uint32_t frameIndex =m_swapChain->GetCurrentImageIndex();

        // Output is where the scene is displayed inside the swap chain: the editor viewport
        // panel, or the whole swap chain.
        const Math::Vector2Int swapChainSize(
            m_swapChain->GetDescriptor().m_dimensions.m_imageWidth,
            m_swapChain->GetDescriptor().m_dimensions.m_imageHeight);
        auto* ui = Service<UI::UIBaseSystem>::Get();
        const Math::Vector2Int outputOrigin = ui ? ui->GetFrameBufferPos()  : Math::Vector2Int(0, 0);
        const Math::Vector2Int outputSize   = ui ? ui->GetFrameBufferSize() : swapChainSize;
        // Screen percentage goes here. Must stay 1:1 until a temporal upscaler sits between
        // SceneColor and Tonemap, which reads its input pixel for pixel.
        const Math::Vector2Int renderSize = outputSize;

        // Produce this frame's views, then encode all of them in one place. A view created
        // just now still gets picked up by CompileShaderInputs later in this same frame.
        m_cameraViewSystem.Update(renderSize, outputOrigin, outputSize, swapChainSize, time);
        // After the camera views: a directional light's ortho box follows the main view.
        m_shadowViewSystem.Update();
        // After the tiles are granted, before SceneBindingSystem marshals the slot it hands
        // each light into LightData.
        m_shadowMaskSystem.Update();
        m_viewBindingSystem.Update(time);

        m_sceneBindingSystem.Update(frameIndex, time);
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

        m_renderGraph.ExecutePipeline(passContext, frameIndex, renderSize, outputSize);
        m_swapChain->Present();
    }
}
