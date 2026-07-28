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
#include <Pass/PassAccess.h>
#include <Pass/RenderPass.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphCompiler.h>
#include <RenderGraph/RenderGraphExecuter.h>
#include <RenderGraph/RenderGraphUtils.h>

#include <View/ViewTags.h>

#include <Resource/AssetManagerInterface.h>

#include <Drawable/DrawTag.h>    // FullScreenTriangleTag
#include <Skybox/Components.h>   // Skybox::ActiveSkyCubeTag

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
            .Accepts<FullScreenTriangleTag>()
            .Binds<MainViewTag>()
            .Build([&, cfg](RenderGraphBuilder& builder)
            {
                Render::ImageAttachmentBindInfo colorBind;
                colorBind.m_slot  = RHI::InputName("SceneColor");
                colorBind.m_usage = RHI::AttachmentUsage::RenderTarget;
                colorBind.m_stage = RHI::AttachmentStage::ColorAttachmentOutput;
                colorBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                colorBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.WriteImageAttachment<SPARK_PASS_TAG("SkyboxPass")>(
                    RHI::AttachmentId("SceneColor"), colorBind);

                Render::ImageAttachmentBindInfo depthBind;
                depthBind.m_slot  = RHI::InputName("SceneDepth");
                depthBind.m_usage = RHI::AttachmentUsage::DepthStencil;
                depthBind.m_stage = RHI::AttachmentStage::EarlyFragmentTest | RHI::AttachmentStage::LateFragmentTest;
                depthBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                depthBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.ReadImageAttachment<SPARK_PASS_TAG("SkyboxPass")>(
                    RHI::AttachmentId("SceneDepth"), depthBind);

                // Import the active skybox cube (SkyboxSystem tags it ActiveSkyCubeTag at
                // creation) once it is materialized AND its upload has been submitted — see
                // IsResourceReady for why both are required. Not ready -> no import,
                // g_SkyCube stays unbound, sky renders black for this frame.
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();
                RHI::RHIHandle cube = RHI::NullHandle;
                rhiCtx.GetView<Skybox::ActiveSkyCubeTag>(Exclude<DeadTag>).each(
                    [&](RHI::RHIHandle e) { cube = e; });
                if (IsResourceReady(rhiCtx, cube))
                {
                    Render::ImportedImageAttachmentBindInfo cubeBind;
                    cubeBind.m_slot           = RHI::InputName("SkyCube");
                    cubeBind.m_image          = cube;
                    cubeBind.m_viewDescriptor = RHI::ImageViewDescriptor::CreateCubemap();
                    cubeBind.m_access         = RHI::AttachmentAccess::Read;
                    cubeBind.m_usage          = RHI::AttachmentUsage::Shader;
                    cubeBind.m_stage          = RHI::AttachmentStage::FragmentShader;
                    cubeBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                    cubeBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;
                    builder.ImportImageAttachment<SPARK_PASS_TAG("SkyboxPass")>(
                        RHI::AttachmentId("SkyCube"), cubeBind);
                }
            })
            .Compile([](RenderGraphCompiler& compiler)
            {
                // Only when Build imported the cube this frame — mirrors Build's gate
                // (active cube + IsResourceReady) so no unbound-slot lookup runs.
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();
                RHI::RHIHandle cube = RHI::NullHandle;
                rhiCtx.GetView<Skybox::ActiveSkyCubeTag>(Exclude<DeadTag>).each(
                    [&](RHI::RHIHandle e) { cube = e; });
                if (!IsResourceReady(rhiCtx, cube))
                {
                    return;
                }
                // Constant sky sampler — change-detected, so this per-frame set is a no-op
                // after the first bind (all this pass's shader-resource binding lives here now).
                SetPassShaderSampler<SPARK_PASS_TAG("SkyboxPass")>(
                    2, RHI::InputName("g_SkySampler"),
                    RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp));
                RHI::ImageView* view = FindPassAttachmentImageView<SPARK_PASS_TAG("SkyboxPass")>(
                    rhiCtx, RHI::InputName("SkyCube"), compiler.GetFrameIndex());
                if (view)
                {
                    SetPassShaderImage<SPARK_PASS_TAG("SkyboxPass")>(2, RHI::InputName("g_SkyCube"), view);
                }
            })
            .Execute([](ExecuteWork& work, RenderGraphExecuter&)
            {
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
