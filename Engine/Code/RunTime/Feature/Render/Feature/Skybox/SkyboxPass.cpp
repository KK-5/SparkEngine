#include "SkyboxPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/RenderPass.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphCompiler.h>
#include <RenderGraph/RenderGraphExecuter.h>
#include <RenderGraph/RenderGraphUtils.h>

#include <Binding/Scene/SceneBinding.h>

#include <View/ViewTags.h>

#include <Resource/AssetManagerInterface.h>

#include <Skybox/Components.h>   // Skybox::ActiveSkyCubeTag

namespace Spark::Render
{
    RenderPassConfig SkyboxPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/Skybox/Skybox.hlsl");
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
        // (NDC z = 0, reversed-Z) and must survive only where no nearer opaque depth was
        // written. GreaterEqual (not Greater) is required — the sky's z equals the depth
        // clear value 0.0, so a strict Greater would reject every sky pixel.
        states.m_depthStencilState.m_depth.m_enable    = 1;
        states.m_depthStencilState.m_depth.m_writeMask = RHI::DepthWriteMask::Zero;
        states.m_depthStencilState.m_depth.m_func      = RHI::ComparisonFunc::GreaterEqual;
        states.m_depthStencilState.m_stencil.m_enable  = 0;
        states.m_rasterState.m_cullMode                = RHI::CullMode::None; // full-screen triangle

        RenderPassConfig cfg;
        cfg.m_vertexShader       = shaderAsset;
        cfg.m_fragmentShader     = shaderAsset;
        cfg.m_renderTargetLayout = rt;
        cfg.m_inputLayout        = input;
        cfg.m_renderStates       = states;


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
            .Binds<MainSceneTag>()
            .RendersView<MainViewTag>()
            .Build([](RenderPassScopes& p)
            {
                RHI::AttachmentLoadStoreAction load;
                load.m_loadAction  = RHI::AttachmentLoadAction::Load;
                load.m_storeAction = RHI::AttachmentStoreAction::Store;

                auto s = p.Scope();
                s.RenderTarget(RHI::AttachmentId("SceneColor"), load);
                s.DepthRead(RHI::AttachmentId("SceneDepth"));
                s.Sampler(RHI::InputName("g_SkySampler"),
                    RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp));

                // The active skybox cube (SkyboxSystem tags it ActiveSkyCubeTag at creation),
                // once it is materialized AND its upload has been submitted — see IsResourceReady
                // for why both are required. No cube -> nothing drawn, and g_SkyCube, bound by
                // nothing, is nulled rather than left holding a deleted skybox.
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();
                RHI::RHIHandle cube = RHI::NullHandle;
                rhiCtx.GetView<Skybox::ActiveSkyCubeTag>(Exclude<DeadTag>).each(
                    [&](RHI::RHIHandle e) { cube = e; });
                if (!IsResourceReady(rhiCtx, cube))
                {
                    return;
                }

                p.Import(RHI::AttachmentId("SkyCube"), cube);
                s.Read(RHI::AttachmentId("SkyCube"))
                    .View(RHI::ImageViewDescriptor::CreateCubemap())
                    .Bind(RHI::InputName("g_SkyCube"));

                s.Draw(RHI::DrawLinear(3, 0)); // full-screen triangle
            })
            .Finalize()
        ;
    }
}
