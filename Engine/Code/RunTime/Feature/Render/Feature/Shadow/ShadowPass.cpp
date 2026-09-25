#include "ShadowPass.h"

#include <CoreComponents/Tags.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Component/Component.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>

#include <Pass/PassContext.h>
#include <Pass/RenderPass.h>

#include <Drawable/DrawTag.h>
#include <View/ViewTags.h>
#include <Binding/Instance/InstanceBinding.h>
#include <View/ShadowAtlasLayout.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphUtils.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    RenderPassConfig ShadowPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/DepthOnly/DepthOnly.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[ShadowPass] Failed to loaded shader DepthOnly.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 0;
        rt.m_depthStencilFormat   = kShadowAtlasFormat;

        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        builder.AddBuffer()->Channel("POSITION", 0, RHI::Format::R32G32B32_FLOAT);
        builder.AddBuffer(RHI::StreamStepFunction::PerInstance, 1)
               ->Channel("INSTANCE_INDEX", 0, RHI::Format::R32_UINT);
        RHI::InputStreamLayout input = builder.End();

        // Slope-scaled only; the constant term is applied per light at sample time
        // (ShadowViewData::depthBias).
        RHI::RenderStates states;
        states.m_depthStencilState.m_depth.m_enable    = 1;
        states.m_depthStencilState.m_depth.m_writeMask = RHI::DepthWriteMask::All;
        states.m_depthStencilState.m_depth.m_func      = RHI::ComparisonFunc::Greater;
        states.m_depthStencilState.m_stencil.m_enable  = 0;
        states.m_rasterState.m_cullMode                = RHI::CullMode::Back;
        // Negative: reversed-Z pushes casters away from the light toward 0.
        states.m_rasterState.m_depthBiasSlopeScale     = -2.0f;

        // Depth pancaking. A directional light's near plane hugs the region it shadows, so a
        // caster standing between the light and that region falls in front of it — and would
        // be clipped away, taking its shadow with it. With clipping off the rasterizer keeps
        // the triangle and clamps its depth to 1 (near, reversed-Z) instead: it still
        // occludes, and against a receiver it is genuinely in front of. What it loses is depth
        // ordering AMONG the pancaked casters, which no receiver inside the volume can observe.
        //
        // The alternative — pulling the near plane back far enough to contain them — costs
        // depth range, which is the same currency the depth bias is denominated in.
        states.m_rasterState.m_depthClipEnable         = 0;

        RenderPassConfig cfg;
        cfg.m_vertexShader       = shaderAsset;
        cfg.m_fragmentShader     = shaderAsset;
        cfg.m_renderTargetLayout = rt;
        cfg.m_inputLayout        = input;
        cfg.m_renderStates       = states;
        return cfg;
    }

    void ShadowPass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "ShadowPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(cfg.m_vertexShader)
            .FragmentShader(cfg.m_fragmentShader)
            .InputLayout(cfg.m_inputLayout)
            .RenderTargetLayout(cfg.m_renderTargetLayout)
            .RenderStates(cfg.m_renderStates)
            .Binds<InstanceBindingTag>()
            .RendersView<ShadowViewTag>()
            .BuildScopes([](RenderPassScopes& p)
            {
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();

                // Until the atlas exists the pass declares nothing and is skipped.
                RHI::RHIHandle atlas = RHI::NullHandle;
                rhiCtx.GetView<ShadowAtlasTag>(Exclude<DeadTag>).each(
                    [&](RHI::RHIHandle e) { atlas = e; });
                if (!IsResourceReady(rhiCtx, atlas))
                {
                    return;
                }
                p.Import(RHI::AttachmentId("ShadowAtlas"), atlas);

                // One render pass for every view, so this clears the whole atlas — a tile no
                // light owns holds the clear value.
                RHI::AttachmentLoadStoreAction clear;
                clear.m_clearValue  = RHI::ClearValue::CreateDepth(0.0f);
                clear.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                clear.m_storeAction = RHI::AttachmentStoreAction::Store;

                auto s = p.Scope();
                s.DepthWrite(RHI::AttachmentId("ShadowAtlas"), clear);
                s.Accepts<ShadowCasterTag>();
            })
            .Finalize();
    }
}
