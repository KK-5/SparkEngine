#include "TemporalAAPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/RenderPass.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphCompiler.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <View/ViewTags.h>
#include <View/MainView.h>
#include <View/ViewComponents.h>

#include <Feature/PostProcess/PostProcessResources.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    RenderPassConfig TemporalAAPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/TemporalAA/TemporalAA.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[TemporalAAPass] Failed to load shader TemporalAA.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // Every pixel is written, so no depth test and no depth attachment.
        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 1;
        rt.m_colorFormats[0]      = RHI::Format::R16G16B16A16_FLOAT;

        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        RHI::InputStreamLayout input = builder.End();

        RHI::RenderStates states;
        states.m_depthStencilState.m_depth.m_enable   = 0;
        states.m_depthStencilState.m_stencil.m_enable = 0;
        states.m_rasterState.m_cullMode               = RHI::CullMode::None;

        RenderPassConfig cfg;
        cfg.m_vertexShader       = shaderAsset;
        cfg.m_fragmentShader     = shaderAsset;
        cfg.m_renderTargetLayout = rt;
        cfg.m_inputLayout        = input;
        cfg.m_renderStates       = states;
        return cfg;
    }

    void TemporalAAPass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "TemporalAAPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(cfg.m_vertexShader)
            .FragmentShader(cfg.m_fragmentShader)
            .InputLayout(cfg.m_inputLayout)
            .RenderTargetLayout(cfg.m_renderTargetLayout)
            .RenderStates(cfg.m_renderStates)
            .Binds<>()
            .RendersView<MainViewTag>()
            .Build([](RenderPassScopes& p)
            {
                // Declaring nothing skips the pass this frame.
                const ViewTemporalAA* settings = FindMainViewComponent<ViewTemporalAA>(*RHI::RHIExecuteContext::Current());
                if (!settings)
                {
                    return;
                }

                const auto size = p.GetRenderSize();
                p.CreateImage(PostProcess::TemporalAAName(), RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead,
                    size.x, size.y, RHI::Format::R16G16B16A16_FLOAT));

                RHI::AttachmentLoadStoreAction clear;
                clear.m_clearValue  = RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 0.f);
                clear.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                clear.m_storeAction = RHI::AttachmentStoreAction::Store;

                auto s = p.Scope();
                s.RenderTarget(PostProcess::TemporalAAName(), clear);
                // After the write: a previous-frame read needs this frame's name declared.
                const ShaderAttachment history =
                    s.ReadPrevious(PostProcess::TemporalAAName()).Bind(RHI::InputName("g_History"));
                s.Read(RHI::AttachmentId("SceneColor")).Bind(RHI::InputName("g_SceneColor"));
                s.Read(RHI::AttachmentId("ResolvedVelocity")).Bind(RHI::InputName("g_Velocity"));
                // Same R32_FLOAT shader-read view over the typeless depth as LightingPass.
                s.Read(RHI::AttachmentId("SceneDepth")).Format(RHI::Format::R32_FLOAT).Bind(RHI::InputName("g_Depth"));

                s.Sampler(RHI::InputName("g_LinearSampler"),
                    RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp));

                const uint32_t historyValid = history.IsPreviousFrameMissing() ? 0u : 1u;
                s.Constant(RHI::InputName("g_TemporalAAHistoryValid"), historyValid);
                s.Constant(RHI::InputName("g_TemporalAACurrentFrameWeight"), settings->m_currentFrameWeight);
                s.Constant(RHI::InputName("g_TemporalAAMotionFrameWeight"), settings->m_motionFrameWeight);
                s.Constant(RHI::InputName("g_TemporalAAVarianceClipGamma"), settings->m_varianceClipGamma);
                s.Constant(RHI::InputName("g_TemporalAAFilterSize"), settings->m_filterSize);

                s.Draw(RHI::DrawLinear(3, 0)); // full-screen triangle
            })
            .Finalize()
        ;
    }
}
