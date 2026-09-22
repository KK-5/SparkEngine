#include "LightingPass.h"

#include <CoreComponents/Tags.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/RenderPass.h>
#include <Pass/PassAccess.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphCompiler.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <View/ViewTags.h>
#include <Feature/ShadowProjection/ShadowProjectionPass.h>
#include <Binding/Scene/SceneBinding.h>

#include <RenderGraph/RenderGraphUtils.h>

#include <Drawable/DrawTag.h>    // FullScreenTriangleTag

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        // GBuffer attachment slot name (matches GBufferPass) → HLSL shader input name.
        struct GBufferBinding
        {
            const char* m_slot;
            const char* m_input;
        };

        constexpr GBufferBinding s_gbufferBindings[] = {
            { "GBufferNormal",    "g_GBufferNormal"    },
            { "GBufferSurface",   "g_GBufferSurface"   },
            { "GBufferBaseColor", "g_GBufferBaseColor" },
        };

        // SceneDepth is sampled (not the color GBuffer) to reconstruct world position.
        // It is viewed as R32_FLOAT (the depth resource is R32_TYPELESS underneath, so a
        // shader-read R32_FLOAT view is valid) and forced to a ShaderRead-only view so
        // ImageView init does not also try to build a DSV at the R32_FLOAT override.
        constexpr const char* s_depthSlot  = "SceneDepth";
        constexpr const char* s_depthInput = "g_Depth";

        // Screen-space visibility, produced by ShadowProjectionPass. Point-sampled 1:1 like
        // the GBuffer, so no sampler.
        constexpr const char* s_maskSlot  = "ShadowMask";
        constexpr const char* s_maskInput = "g_ShadowMask";
    }

    RenderPassConfig LightingPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/Lighting/Lighting.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[LightingPass] Failed to load shader Lighting.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // Single color target (SceneColor, owned by GBufferPass) + read-only SceneDepth.
        // The full-screen triangle sits at the far plane (z=0, reversed-Z) and is
        // depth-tested Less against SceneDepth, so only pixels with geometry survive.
        // Depth is never written (writeMask Zero), so the DSV stays read-only and can
        // coexist with the depth SRV.
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
        states.m_rasterState.m_cullMode                = RHI::CullMode::None; // full-screen triangle

        // Additive: SceneColor already holds the emissive GBufferPass wrote, and the other
        // lighting passes blend onto the same target. Alpha keeps the destination — nothing
        // reads it, and summing it would only confuse a capture.
        auto& blend = states.m_blendState.m_targets[0];
        blend.m_enable          = 1;
        blend.m_blendSource     = RHI::BlendFactor::One;
        blend.m_blendDest       = RHI::BlendFactor::One;
        blend.m_blendOp         = RHI::BlendOp::Add;
        blend.m_blendAlphaSource = RHI::BlendFactor::Zero;
        blend.m_blendAlphaDest   = RHI::BlendFactor::One;
        blend.m_blendAlphaOp     = RHI::BlendOp::Add;

        RenderPassConfig cfg;
        cfg.m_vertexShader       = shaderAsset;
        cfg.m_fragmentShader     = shaderAsset;
        cfg.m_renderTargetLayout = rt;
        cfg.m_inputLayout        = input;
        cfg.m_renderStates       = states;


        return cfg;
    }

    void LightingPass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "LightingPass")
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
                // GBufferPass owns SceneColor and left the emissive in it; this pass
                // blends its lighting on top, so the contents are loaded, not cleared.
                Render::ImageAttachmentBindInfo colorBind;
                colorBind.m_slot  = RHI::InputName("SceneColor");
                colorBind.m_usage = RHI::AttachmentUsage::RenderTarget;
                colorBind.m_stage = RHI::AttachmentStage::ColorAttachmentOutput;
                colorBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                colorBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.WriteImageAttachment<SPARK_PASS_TAG("LightingPass")>(
                    RHI::AttachmentId("SceneColor"), colorBind);

                // Read the GBuffer color targets as shader resources. Declaring them
                // here makes the graph (a) order this pass after GBufferPass and (b)
                // transition them from RenderTarget to shader-read before this pass runs.
                // The actual view→SRG binding happens in the Compile hook below.
                for (const auto& gb : s_gbufferBindings)
                {
                    Render::ImageAttachmentBindInfo readBind;
                    readBind.m_slot  = RHI::InputName(gb.m_slot);
                    readBind.m_usage = RHI::AttachmentUsage::Shader;
                    readBind.m_stage = RHI::AttachmentStage::FragmentShader;
                    readBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                    readBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                    builder.ReadImageAttachment<SPARK_PASS_TAG("LightingPass")>(
                        RHI::AttachmentId(gb.m_slot), readBind);
                }

                // Read SceneDepth as a shader resource to reconstruct world position.
                // The R32_FLOAT override + ShaderRead-only view descriptor make the
                // resolved view a plain SRV over the (typeless) depth resource; the graph
                // transitions SceneDepth from read-only depth to shader-read for this pass
                // (the skybox pass after transitions it back to read-only depth).
                Render::ImageAttachmentBindInfo depthBind;
                depthBind.m_slot  = RHI::InputName(s_depthSlot);
                depthBind.m_usage = RHI::AttachmentUsage::Shader;
                depthBind.m_stage = RHI::AttachmentStage::FragmentShader;
                depthBind.m_view.m_overrideFormat    = RHI::Format::R32_FLOAT;
                depthBind.m_view.m_overrideBindFlags = RHI::ImageBindFlags::ShaderRead;
                depthBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                depthBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.ReadImageAttachment<SPARK_PASS_TAG("LightingPass")>(
                    RHI::AttachmentId(s_depthSlot), depthBind);

                // Also bind SceneDepth as a read-only depth-stencil attachment so the
                // rasterizer depth-tests against it (far-plane z=0, func Less) and culls
                // sky/uncovered pixels before the PS. Same resource as the SRV above — the
                // compiler folds both into one DepthStencilRead | ShaderSampledRead barrier.
                // No view override: this resolves the resource's D32_FLOAT read-only DSV,
                // distinct from the R32_FLOAT SRV above in the view cache.
                Render::ImageAttachmentBindInfo depthTestBind;
                depthTestBind.m_slot  = RHI::InputName("SceneDepthTest");
                depthTestBind.m_usage = RHI::AttachmentUsage::DepthStencil;
                depthTestBind.m_stage = RHI::AttachmentStage::EarlyFragmentTest | RHI::AttachmentStage::LateFragmentTest;
                depthTestBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                depthTestBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.ReadImageAttachment<SPARK_PASS_TAG("LightingPass")>(
                    RHI::AttachmentId(s_depthSlot), depthTestBind);

                // Declared only once ShadowProjectionPass has produced it: with no shadowed
                // lights there is no mask, and every m_shadowMaskIndex is -1, so the shader
                // never reaches the sampler. This declaration is also the edge that orders
                // the projection before this pass.
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();
                if (ShadowMaskSliceCount(rhiCtx) == 0)
                {
                    return;
                }

                Render::ImageAttachmentBindInfo maskBind;
                maskBind.m_slot  = RHI::InputName(s_maskSlot);
                maskBind.m_usage = RHI::AttachmentUsage::Shader;
                maskBind.m_stage = RHI::AttachmentStage::FragmentShader;
                maskBind.m_view.m_isArray = 1;
                maskBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                maskBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.ReadImageAttachment<SPARK_PASS_TAG("LightingPass")>(
                    RHI::AttachmentId(s_maskSlot), maskBind);
            })
            .Compile([](RenderGraphCompiler& compiler)
            {
                // Post-CompileTransientResources, pre-CompileShaderInputs: the GBuffer
                // is materialized, so resolve each target's view and stage it into this
                // pass's space2 SRG (auto-created by Finalize). SetPassShaderImage
                // marks the SRG dirty so CompileShaderInputs recompiles it with the views.
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();
                const uint32_t frameIndex = compiler.GetFrameIndex();

                for (const auto& gb : s_gbufferBindings)
                {
                    RHI::ImageView* view = FindPassAttachmentImageView<SPARK_PASS_TAG("LightingPass")>(
                        rhiCtx, RHI::InputName(gb.m_slot), frameIndex);
                    if (!view)
                    {
                        continue;
                    }
                    SetPassShaderImage<SPARK_PASS_TAG("LightingPass")>(
                        2, RHI::InputName(gb.m_input), view);
                }

                // SceneDepth → g_Depth (t3). Resolved with the R32_FLOAT / ShaderRead view
                // descriptor declared in Build, so this is the depth resource's SRV.
                RHI::ImageView* depthView = FindPassAttachmentImageView<SPARK_PASS_TAG("LightingPass")>(
                    rhiCtx, RHI::InputName(s_depthSlot), frameIndex);
                if (depthView)
                {
                    SetPassShaderImage<SPARK_PASS_TAG("LightingPass")>(
                        2, RHI::InputName(s_depthInput), depthView);
                }

                // Null on the frames Build declared no mask; every m_shadowMaskIndex is then
                // -1, so the shader never reaches it.
                RHI::ImageView* maskView = FindPassAttachmentImageView<SPARK_PASS_TAG("LightingPass")>(
                    rhiCtx, RHI::InputName(s_maskSlot), frameIndex);
                if (maskView)
                {
                    SetPassShaderImage<SPARK_PASS_TAG("LightingPass")>(
                        2, RHI::InputName(s_maskInput), maskView);
                }
            })
            .Finalize()
        ;
    }
}
