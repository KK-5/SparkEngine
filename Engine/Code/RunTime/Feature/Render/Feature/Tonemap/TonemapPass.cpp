#include "TonemapPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/Image/ImageView.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/RenderPass.h>
#include <Pass/PassAccess.h>

#include <Drawable/DrawTag.h>    // FullScreenTriangleTag

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphCompiler.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <View/ViewTags.h>

#include <Feature/TemporalAA/TemporalAAPass.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    RenderPassConfig TonemapPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/Tonemap/Tonemap.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[TonemapPass] Failed to load shader Tonemap.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // Color-only pass writing the LDR swap chain — no depth. The full-screen triangle
        // covers every pixel, so there is no depth test and no depth attachment
        // (m_depthStencilFormat stays Format::Unknown). The color format must match the
        // swap chain (R8G8B8A8_UNORM, see RenderSystem::InitRHIData).
        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 1;
        rt.m_colorFormats[0]      = RHI::Format::R8G8B8A8_UNORM;

        // Empty input layout: the full-screen triangle is generated from SV_VertexID.
        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        RHI::InputStreamLayout input = builder.End();

        RHI::RenderStates states;
        states.m_depthStencilState.m_depth.m_enable   = 0;
        states.m_depthStencilState.m_stencil.m_enable = 0;
        states.m_rasterState.m_cullMode               = RHI::CullMode::None; // full-screen triangle

        RenderPassConfig cfg;
        cfg.m_vertexShader       = shaderAsset;
        cfg.m_fragmentShader     = shaderAsset;
        cfg.m_renderTargetLayout = rt;
        cfg.m_inputLayout        = input;
        cfg.m_renderStates       = states;


        return cfg;
    }

    void TonemapPass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "TonemapPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(cfg.m_vertexShader)
            .FragmentShader(cfg.m_fragmentShader)
            .InputLayout(cfg.m_inputLayout)
            .RenderTargetLayout(cfg.m_renderTargetLayout)
            .RenderStates(cfg.m_renderStates)
            .Accepts<FullScreenTriangleTag>()
            .Binds<>()
            .RendersView<OutputViewTag>()
            .BuildScopes([](RenderPassScopes& p)
            {
                // TonemapPass is the first pass to touch the swap chain, so it owns the import;
                // UIPass writes the same imported "SwapChain" afterwards. Cleared (black) so the
                // warmup frame — before SceneColor is ready and the draw is dropped — is not garbage.
                p.Import(RHI::AttachmentId("SwapChain"), p.GetCurrentSwapChainResource());

                RHI::AttachmentLoadStoreAction clear;
                clear.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                clear.m_storeAction = RHI::AttachmentStoreAction::Store;
                clear.m_clearValue  = RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 1.f);

                // The HDR scene color: TemporalAAPass's output when it runs, SceneColor otherwise.
                const bool temporalAA = TemporalAAPass::FindMainViewSettings(*RHI::RHIExecuteContext::Current()) != nullptr;

                auto s = p.Scope();
                s.RenderTarget(RHI::AttachmentId("SwapChain"), clear);
                s.Read(RHI::AttachmentId(temporalAA ? "TemporalAA" : "SceneColor")).Bind(RHI::InputName("g_SceneColor"));
            })
            .Finalize()
        ;
    }
}
