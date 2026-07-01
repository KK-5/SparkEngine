#include "SkyboxPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>

#include <Pass/PassContext.h>
#include <Pass/RenderPass.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphExecuter.h>

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

        // Color-only: writes the existing SceneColor, no depth attachment in this first
        // cut (there is no opaque geometry yet, so the LessEqual depth interaction is a
        // no-op; it is added once geometry exists).
        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 1;
        rt.m_colorFormats[0]      = RHI::Format::R8G8B8A8_UNORM;

        // Empty input layout: the full-screen triangle is generated from SV_VertexID,
        // there is no vertex buffer.
        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        RHI::InputStreamLayout input = builder.End();

        RHI::RenderStates states;
        states.m_depthStencilState.m_depth.m_enable   = 0; // no depth test/write (first cut)
        states.m_depthStencilState.m_stencil.m_enable = 0;
        states.m_rasterState.m_cullMode               = RHI::CullMode::None; // full-screen triangle

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
