#include "IndirectDiffusePass.h"

#include <CoreComponents/Tags.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/Image/ImageView.h>

#include <Pass/PassAccess.h>
#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/RenderPass.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphCompiler.h>

#include <Drawable/DrawTag.h>    // FullScreenTriangleTag
#include <Binding/Scene/SceneBinding.h>
#include <View/ViewTags.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        // GBuffer attachment slot name (matches GBufferPass) → HLSL shader input name.
        struct SceneTexture
        {
            const char* m_slot;
            const char* m_input;
        };

        constexpr SceneTexture s_gbufferTextures[] = {
            { "GBufferNormal",    "g_GBufferNormal"    },
            { "GBufferSurface",   "g_GBufferSurface"   },
            { "GBufferBaseColor", "g_GBufferBaseColor" },
        };

        // Depth-test only, never sampled: irradiance is view independent, so this pass
        // reconstructs no world position and declares no depth SRV.
        constexpr const char* s_depthSlot = "SceneDepth";
    }

    RenderPassConfig IndirectDiffusePass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/Lighting/IndirectDiffuse.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[IndirectDiffusePass] Failed to load shader IndirectDiffuse.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // Single color target (SceneColor, owned by GBufferPass) + read-only SceneDepth.
        // The full-screen triangle sits at the far plane (z=0, reversed-Z) and is depth-tested
        // Less against SceneDepth, so only pixels with geometry survive. Depth is never
        // written (writeMask Zero), so the DSV stays read-only and can coexist with the SRV.
        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 1;
        rt.m_colorFormats[0]      = RHI::Format::R16G16B16A16_FLOAT;
        rt.m_depthStencilFormat   = RHI::Format::D32_FLOAT;

        // Empty input layout: the full-screen triangle is generated from SV_VertexID.
        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        RHI::InputStreamLayout input = builder.End();

        RHI::RenderStates states;
        states.m_depthStencilState.m_depth.m_enable    = 1;
        states.m_depthStencilState.m_depth.m_writeMask = RHI::DepthWriteMask::Zero;
        states.m_depthStencilState.m_depth.m_func      = RHI::ComparisonFunc::Less;
        states.m_depthStencilState.m_stencil.m_enable  = 0;
        states.m_rasterState.m_cullMode                = RHI::CullMode::None;

        // Additive: SceneColor already holds the emissive GBufferPass wrote, and the other
        // two lighting passes blend onto the same target. Alpha keeps the destination.
        auto& blend = states.m_blendState.m_targets[0];
        blend.m_enable           = 1;
        blend.m_blendSource      = RHI::BlendFactor::One;
        blend.m_blendDest        = RHI::BlendFactor::One;
        blend.m_blendOp          = RHI::BlendOp::Add;
        blend.m_blendAlphaSource  = RHI::BlendFactor::Zero;
        blend.m_blendAlphaDest    = RHI::BlendFactor::One;
        blend.m_blendAlphaOp      = RHI::BlendOp::Add;

        RenderPassConfig cfg;
        cfg.m_vertexShader       = shaderAsset;
        cfg.m_fragmentShader     = shaderAsset;
        cfg.m_renderTargetLayout = rt;
        cfg.m_inputLayout        = input;
        cfg.m_renderStates       = states;
        return cfg;
    }

    void IndirectDiffusePass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "IndirectDiffusePass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(cfg.m_vertexShader)
            .FragmentShader(cfg.m_fragmentShader)
            .InputLayout(cfg.m_inputLayout)
            .RenderTargetLayout(cfg.m_renderTargetLayout)
            .RenderStates(cfg.m_renderStates)
            .Accepts<FullScreenTriangleTag>()
            .Binds<MainSceneTag>()
            .RendersView<MainViewTag>()
            .Build([](RenderGraphBuilder& builder)
            {
                Render::ImageAttachmentBindInfo colorBind;
                colorBind.m_slot  = RHI::InputName("SceneColor");
                colorBind.m_usage = RHI::AttachmentUsage::RenderTarget;
                colorBind.m_stage = RHI::AttachmentStage::ColorAttachmentOutput;
                colorBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                colorBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.WriteImageAttachment<SPARK_PASS_TAG("IndirectDiffusePass")>(
                    RHI::AttachmentId("SceneColor"), colorBind);

                // Declaring the GBuffer reads here makes the graph (a) order this pass after
                // GBufferPass and (b) transition them to shader-read. The view→SRG binding
                // happens in the Compile hook below.
                for (const auto& tex : s_gbufferTextures)
                {
                    Render::ImageAttachmentBindInfo readBind;
                    readBind.m_slot  = RHI::InputName(tex.m_slot);
                    readBind.m_usage = RHI::AttachmentUsage::Shader;
                    readBind.m_stage = RHI::AttachmentStage::FragmentShader;
                    readBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                    readBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                    builder.ReadImageAttachment<SPARK_PASS_TAG("IndirectDiffusePass")>(
                        RHI::AttachmentId(tex.m_slot), readBind);
                }

                // Read-only depth-stencil attachment so the rasterizer depth-tests against it
                // and culls sky pixels before the PS. No view override: this resolves the
                // resource's D32_FLOAT read-only DSV.
                Render::ImageAttachmentBindInfo depthTestBind;
                depthTestBind.m_slot  = RHI::InputName("SceneDepthTest");
                depthTestBind.m_usage = RHI::AttachmentUsage::DepthStencil;
                depthTestBind.m_stage = RHI::AttachmentStage::EarlyFragmentTest | RHI::AttachmentStage::LateFragmentTest;
                depthTestBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                depthTestBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.ReadImageAttachment<SPARK_PASS_TAG("IndirectDiffusePass")>(
                    RHI::AttachmentId(s_depthSlot), depthTestBind);
            })
            .Compile([](RenderGraphCompiler& compiler)
            {
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();
                const uint32_t frameIndex = compiler.GetFrameIndex();

                for (const auto& tex : s_gbufferTextures)
                {
                    RHI::ImageView* view = FindPassAttachmentImageView<SPARK_PASS_TAG("IndirectDiffusePass")>(
                        rhiCtx, RHI::InputName(tex.m_slot), frameIndex);
                    if (!view)
                    {
                        continue;
                    }
                    SetPassShaderImage<SPARK_PASS_TAG("IndirectDiffusePass")>(
                        kPerPassSpaceId, RHI::InputName(tex.m_input), view);
                }
            })
            .Finalize()
        ;
    }
}
