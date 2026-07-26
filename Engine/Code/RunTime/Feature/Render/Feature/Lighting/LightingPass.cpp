#include "LightingPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/Image/ImageView.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/RenderPass.h>
#include <Pass/PassAccess.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphCompiler.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <View/ViewTags.h>
#include <SceneBind/SceneBinding.h>

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
            { "GBufferAlbedo",   "g_Albedo"   },
            { "GBufferNormal",   "g_Normal"   },
            { "GBufferORM",      "g_ORM"      },
            { "GBufferEmissive", "g_Emissive" },
        };

        // SceneDepth is sampled (not the color GBuffer) to reconstruct world position.
        // It is viewed as R32_FLOAT (the depth resource is R32_TYPELESS underneath, so a
        // shader-read R32_FLOAT view is valid) and forced to a ShaderRead-only view so
        // ImageView init does not also try to build a DSV at the R32_FLOAT override.
        constexpr const char* s_depthSlot  = "SceneDepth";
        constexpr const char* s_depthInput = "g_Depth";
    }

    RenderPassConfig LightingPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("Shaders/Lighting/Lighting.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[LightingPass] Failed to load shader Lighting.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // Single color target (SceneColor) + read-only SceneDepth. The full-screen
        // triangle sits at the far plane (z=1) and is depth-tested Greater against
        // SceneDepth: only pixels with geometry (depth < 1) survive, replacing the old
        // shader discard and keeping early-Z. Depth is never written (writeMask Zero),
        // so the DSV stays read-only and can coexist with the depth SRV.
        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 1;
        // SceneColor is linear HDR; this pass writes raw radiance (tonemapping now
        // happens in the final TonemapPass, not here). Must match DepthPrePass's format.
        rt.m_colorFormats[0]      = RHI::Format::R16G16B16A16_FLOAT;
        rt.m_depthStencilFormat   = RHI::Format::D32_FLOAT;

        // Empty input layout: the full-screen triangle is generated from SV_VertexID.
        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        RHI::InputStreamLayout input = builder.End();

        RHI::RenderStates states;
        states.m_depthStencilState.m_depth.m_enable    = 1;
        states.m_depthStencilState.m_depth.m_writeMask = RHI::DepthWriteMask::Zero;
        states.m_depthStencilState.m_depth.m_func      = RHI::ComparisonFunc::Greater;
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

    void LightingPass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "LightingPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(cfg.m_vertexShader)
            .FragmentShader(cfg.m_fragmentShader)
            .InputLayout(cfg.m_inputLayout)
            .RenderTargetLayout(cfg.m_renderTargetLayout)
            .RenderStates(cfg.m_renderStates)
            .ViewportScissor(cfg.m_viewport, cfg.m_scissor)
            .Accepts<SPARK_PASS_TAG("LightingPass")>()
            .Binds<MainViewTag, MainSceneTag>()
            .Build([](RenderGraphBuilder& builder)
            {
                // Create SceneColor here: LightingPass is the first pass that produces
                // scene color (linear HDR), so it owns the resource (DepthPrePass is now
                // depth-only). Clear to the background color; pixels this pass depth-culls
                // (sky) keep the clear for the skybox to fill afterwards. ShaderRead so the
                // final TonemapPass can sample it.
                auto colorDesc = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead,
                    builder.GetRenderSize().x,
                    builder.GetRenderSize().y,
                    RHI::Format::R16G16B16A16_FLOAT);

                Render::ImageAttachmentBindInfo colorBind;
                colorBind.m_slot  = RHI::InputName("SceneColor");
                colorBind.m_usage = RHI::AttachmentUsage::RenderTarget;
                colorBind.m_stage = RHI::AttachmentStage::ColorAttachmentOutput;
                colorBind.m_action.m_clearValue  = RHI::ClearValue::CreateVector4Float(0.1f, 0.1f, 0.15f, 1.f);
                colorBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                colorBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.CreateImageAttachment<SPARK_PASS_TAG("LightingPass")>(
                    RHI::AttachmentId("SceneColor"), colorDesc, colorBind, RHI::AttachmentAccess::Write);

                // Read the three GBuffer color targets as shader resources. Declaring them
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
                // rasterizer depth-tests against it (far-plane z=1, func Greater) and culls
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
            })
            .Compile([](RenderGraphCompiler& compiler)
            {
                // Post-CompileTransientResources, pre-CompileShaderInputs: the GBuffer
                // is materialized, so resolve each target's view and stage it into this
                // pass's space2 SRG (created by LightingProcessor). SetPassShaderImage
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
            })
            .Finalize()
        ;
    }
}
