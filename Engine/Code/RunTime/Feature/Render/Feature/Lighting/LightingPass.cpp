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
            { "GBufferAlbedo", "g_Albedo" },
            { "GBufferNormal", "g_Normal" },
            { "GBufferORM",    "g_ORM"    },
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

        // Single color target (SceneColor). No depth: this is a full-screen shading
        // pass that reads the GBuffer, it does not depth-test.
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
            .Build([](RenderGraphBuilder& builder)
            {
                // Write SceneColor with Load: pixels this pass discards (sky) keep the
                // DepthPrePass clear for the skybox to fill afterwards.
                Render::ImageAttachmentBindInfo colorBind;
                colorBind.m_slot  = RHI::InputName("SceneColor");
                colorBind.m_usage = RHI::AttachmentUsage::RenderTarget;
                colorBind.m_stage = RHI::AttachmentStage::ColorAttachmentOutput;
                colorBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                colorBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.WriteImageAttachment<SPARK_PASS_TAG("LightingPass")>(
                    RHI::AttachmentId("SceneColor"), colorBind);

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
            })
            .Compile([](RenderGraphCompiler& compiler)
            {
                // Post-CompileTransientResources, pre-CompileShaderInputs: the GBuffer
                // is materialized, so resolve each target's view and stage it into this
                // pass's space1 SRG (created by LightingProcessor). SetPassShaderImage
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
                        1, RHI::InputName(gb.m_input), view);
                }

                // SceneDepth → g_Depth (t3). Resolved with the R32_FLOAT / ShaderRead view
                // descriptor declared in Build, so this is the depth resource's SRV.
                RHI::ImageView* depthView = FindPassAttachmentImageView<SPARK_PASS_TAG("LightingPass")>(
                    rhiCtx, RHI::InputName(s_depthSlot), frameIndex);
                if (depthView)
                {
                    SetPassShaderImage<SPARK_PASS_TAG("LightingPass")>(
                        1, RHI::InputName(s_depthInput), depthView);
                }
            })
            .Finalize()
        ;
    }
}
