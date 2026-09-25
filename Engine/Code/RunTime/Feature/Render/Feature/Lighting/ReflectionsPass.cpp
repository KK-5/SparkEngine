#include "ReflectionsPass.h"

#include <CoreComponents/Tags.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/Image/ImageView.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/RenderPass.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphCompiler.h>

#include <Binding/Scene/SceneBinding.h>
#include <View/ViewTags.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        // GBuffer image name (as GBufferPass creates it) → HLSL shader input name.
        struct SceneTexture
        {
            const char* m_name;
            const char* m_input;
        };

        constexpr SceneTexture s_gbufferTextures[] = {
            { "GBufferNormal",    "g_GBufferNormal"    },
            { "GBufferSurface",   "g_GBufferSurface"   },
            { "GBufferBaseColor", "g_GBufferBaseColor" },
        };

        // Sampled to reconstruct world position, which the reflection vector needs.
        constexpr const char* s_depthName  = "SceneDepth";
        constexpr const char* s_depthInput = "g_Depth";
    }

    RenderPassConfig ReflectionsPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/Lighting/Reflections.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[ReflectionsPass] Failed to load shader Reflections.hlsl.");
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

    void ReflectionsPass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "ReflectionsPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(cfg.m_vertexShader)
            .FragmentShader(cfg.m_fragmentShader)
            .InputLayout(cfg.m_inputLayout)
            .RenderTargetLayout(cfg.m_renderTargetLayout)
            .RenderStates(cfg.m_renderStates)
            .Binds<MainSceneTag>()
            .RendersView<MainViewTag>()
            .BuildScopes([](RenderPassScopes& p)
            {
                RHI::AttachmentLoadStoreAction load;
                load.m_loadAction  = RHI::AttachmentLoadAction::Load;
                load.m_storeAction = RHI::AttachmentStoreAction::Store;

                auto s = p.Scope();
                s.RenderTarget(RHI::AttachmentId("SceneColor"), load);
                for (const auto& tex : s_gbufferTextures)
                {
                    s.Read(RHI::AttachmentId(tex.m_name)).Bind(RHI::InputName(tex.m_input));
                }
                s.Read(RHI::AttachmentId(s_depthName)).Format(RHI::Format::R32_FLOAT).Bind(RHI::InputName(s_depthInput));
                // Read-only depth-stencil attachment so the rasterizer depth-tests against it
                // and culls sky pixels before the PS.
                s.DepthRead(RHI::AttachmentId(s_depthName));

                s.Draw(RHI::DrawLinear(3, 0)); // full-screen triangle
            })
            .Finalize()
        ;
    }
}
