#include "ShadowPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Component/Component.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>

#include <Pass/PassContext.h>
#include <Pass/RenderPass.h>

#include <Drawable/DrawTag.h>
#include <View/ViewTags.h>
#include <View/ShadowAtlasLayout.h>

#include <RenderGraph/RenderGraphBuilder.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        //! NullHandle before ShadowViewSystem::Init.
        RHI::RHIHandle FindShadowAtlas(RHI::RHIContext& rhiCtx)
        {
            RHI::RHIHandle atlas = RHI::NullHandle;
            rhiCtx.GetView<ShadowAtlasTag>().each([&](RHI::RHIHandle e) { atlas = e; });
            return atlas;
        }
    }

    RenderPassConfig ShadowPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("Shaders/DepthOnly/DepthOnly.hlsl");
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

        // Bias stays zero until sampling lands and acne is visible. Slope-scaled bias is PSO
        // state, so every view of this pass shares one value — see TODO_MultiViewPlan.md §五.
        RHI::RenderStates states;
        states.m_depthStencilState.m_depth.m_enable    = 1;
        states.m_depthStencilState.m_depth.m_writeMask = RHI::DepthWriteMask::All;
        states.m_depthStencilState.m_depth.m_func      = RHI::ComparisonFunc::Less;
        states.m_depthStencilState.m_stencil.m_enable  = 0;
        states.m_rasterState.m_cullMode                = RHI::CullMode::Back;

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
            .Accepts<ShadowCasterTag>()
            .Binds<>()
            .RendersView<ShadowViewTag>()
            .Build([](RenderGraphBuilder& builder)
            {
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();

                // Before the atlas exists ImportImageAttachment would assert. Declaring
                // nothing leaves the pass without a RenderPassBeginInfo, which the executer
                // skips.
                const RHI::RHIHandle atlas = FindShadowAtlas(rhiCtx);
                if (atlas == RHI::NullHandle || !rhiCtx.Has<RHI::Components::Image>(atlas))
                {
                    return;
                }

                // BeginRenderPass is per pass, so this clears the whole atlas — a tile no
                // light owns holds the clear value.
                ImportedImageAttachmentBindInfo bind;
                bind.m_slot   = RHI::InputName("ShadowAtlasDepth");
                bind.m_image  = atlas;
                bind.m_access = RHI::AttachmentAccess::Write;
                bind.m_usage  = RHI::AttachmentUsage::DepthStencil;
                bind.m_stage  = RHI::AttachmentStage::EarlyFragmentTest |
                                RHI::AttachmentStage::LateFragmentTest;
                bind.m_action.m_clearValue  = RHI::ClearValue::CreateDepth(1.0f);
                bind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                bind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.ImportImageAttachment<SPARK_PASS_TAG("ShadowPass")>(
                    RHI::AttachmentId("ShadowAtlas"), bind);
            })
            .Finalize();
    }
}
