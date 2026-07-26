#include "SkyboxPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>

#include <Pass/PassContext.h>
#include <Pass/RenderPass.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <View/ViewTags.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    RenderPassConfig SkyboxPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("Shaders/Skybox/Skybox.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[SkyboxPass] Failed to load shader Skybox.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // Writes SceneColor and depth-tests (read-only) against SceneDepth from
        // DepthPrePass, so the sky only fills pixels no opaque geometry claimed. The
        // depth format must match the SceneDepth attachment DepthPrePass creates.
        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 1;
        // SceneColor is linear HDR; the sky writes raw radiance (tonemapping moved to
        // the final TonemapPass). Must match DepthPrePass's SceneColor format.
        rt.m_colorFormats[0]      = RHI::Format::R16G16B16A16_FLOAT;
        rt.m_depthStencilFormat   = RHI::Format::D32_FLOAT;

        // Empty input layout: the full-screen triangle is generated from SV_VertexID,
        // there is no vertex buffer.
        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        RHI::InputStreamLayout input = builder.End();

        RHI::RenderStates states;
        // Depth-test against SceneDepth but never write: the sky sits on the far plane
        // (NDC z = 1) and must survive only where no nearer opaque depth was written.
        // LessEqual (not Less) is required — the sky's z equals the depth clear value
        // 1.0, so a strict Less would reject every sky pixel.
        states.m_depthStencilState.m_depth.m_enable    = 1;
        states.m_depthStencilState.m_depth.m_writeMask = RHI::DepthWriteMask::Zero;
        states.m_depthStencilState.m_depth.m_func      = RHI::ComparisonFunc::LessEqual;
        states.m_depthStencilState.m_stencil.m_enable  = 0;
        states.m_rasterState.m_cullMode                = RHI::CullMode::None; // full-screen triangle

        RenderPassConfig cfg;
        cfg.m_vertexShader       = shaderAsset;
        cfg.m_fragmentShader     = shaderAsset;
        cfg.m_renderTargetLayout = rt;
        cfg.m_inputLayout        = input;
        cfg.m_renderStates       = states;

        cfg.m_viewport = RHI::Viewport(0.f, 1920.f, 0.f, 1080.f);
        cfg.m_scissor  = RHI::Scissor(0, 0, 1920, 1080);

        return cfg;
    }

    void SkyboxPass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "SkyboxPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(cfg.m_vertexShader)
            .FragmentShader(cfg.m_fragmentShader)
            .InputLayout(cfg.m_inputLayout)
            .RenderTargetLayout(cfg.m_renderTargetLayout)
            .RenderStates(cfg.m_renderStates)
            .ViewportScissor(cfg.m_viewport, cfg.m_scissor)
            .Accepts<SPARK_PASS_TAG("SkyboxPass")>()
            .Binds<MainViewTag>()
            .Build([&, cfg](RenderGraphBuilder& builder)
            {
                // Write the existing SceneColor with Load so DepthPrePass's clear (and,
                // later, opaque geometry) survives — the sky only fills untouched pixels.
                Render::ImageAttachmentBindInfo colorBind;
                colorBind.m_slot  = RHI::InputName("SceneColor");
                colorBind.m_usage = RHI::AttachmentUsage::RenderTarget;
                colorBind.m_stage = RHI::AttachmentStage::ColorAttachmentOutput;
                colorBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                colorBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.WriteImageAttachment<SPARK_PASS_TAG("SkyboxPass")>(
                    RHI::AttachmentId("SceneColor"), colorBind);

                // Read-only depth test against DepthPrePass's SceneDepth: Load the existing
                // depth to test against, Store it back unchanged. The sky never writes depth
                // — that is enforced by AttachmentAccess::Read (ReadImageAttachment selects a
                // READ_ONLY_DEPTH DSV), not by the store action, so PRESERVE/PRESERVE is the
                // correct pairing. With the LessEqual / no-write render state this discards
                // the sky wherever opaque geometry already wrote a nearer depth.
                Render::ImageAttachmentBindInfo depthBind;
                depthBind.m_slot  = RHI::InputName("SceneDepth");
                depthBind.m_usage = RHI::AttachmentUsage::DepthStencil;
                depthBind.m_stage = RHI::AttachmentStage::EarlyFragmentTest | RHI::AttachmentStage::LateFragmentTest;
                depthBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                depthBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;
                // Stencil actions default to None (SceneDepth is D32_FLOAT — no stencil
                // plane), so the read-only depth DSV stays in DEPTH_READ without the
                // backend synthesizing a stencil write (#538).

                builder.ReadImageAttachment<SPARK_PASS_TAG("SkyboxPass")>(
                    RHI::AttachmentId("SceneDepth"), depthBind);
            })
            .Execute([](ExecuteWork& work, RenderGraphExecuter&)
            {
                // Data-driven: submit whatever DrawItems were tagged for this pass. The
                // SkyboxProcessor builds a single full-screen DrawItem (Path B); the pass
                // does not know or care how it was produced. PSO is auto-bound by the
                // executer, so DrawItem.m_pipelineState is left null.
                auto& rhi = *RHI::RHIExecuteContext::Current();
                rhi.GetView<SPARK_PASS_TAG("SkyboxPass"), RHI::DrawItem>().each(
                [&](RHI::RHIHandle, const RHI::DrawItem& item)
                {
                    work.m_commandList->Submit(item);
                });
            })
            .Finalize()
        ;
    }
}
