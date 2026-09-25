#include "ShadowProjectionPass.h"

#include <EASTL/algorithm.h>

#include <CoreComponents/Tags.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassContext.h>
#include <Pass/RenderPass.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphUtils.h>

#include <Binding/Scene/SceneBinding.h>
#include <View/ShadowAtlasLayout.h>
#include <View/ShadowMaskLayout.h>
#include <View/ViewTags.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        //! Slices the shadowed lights fill this frame: the draw's instance count. Unlike
        //! ShadowMaskSliceCount, 0 when there are none.
        uint32_t ShadowedSliceCount(RHI::RHIContext& ctx)
        {
            for (auto [entity, layout] : ctx.GetView<ShadowMaskLayout>().each())
            {
                return layout.m_sliceCount;
            }
            return 0;
        }
    }

    uint32_t ShadowMaskSliceCount(RHI::RHIContext& ctx)
    {
        RHI::RHIHandle atlas = RHI::NullHandle;
        ctx.GetView<ShadowAtlasTag>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle e) { atlas = e; });
        if (!IsResourceReady(ctx, atlas))
        {
            return 0;
        }

        for (auto [entity, layout] : ctx.GetView<ShadowMaskLayout>().each())
        {
            // At least one slice whenever the mask exists at all. Zero shadowed lights still
            // gets a cleared slice rather than no texture, so the lighting pass binds the
            // same thing every frame.
            return eastl::max(layout.m_sliceCount, 1u);
        }
        return 0;
    }

    RenderPassConfig ShadowProjectionPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/Shadow/ShadowProjection.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[ShadowProjectionPass] Failed to load shader ShadowProjection.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // No depth attachment: SceneDepth is a plain 2D image and ShadowMask is an array, and
        // one render pass renders one layer count for all of its attachments. So the sky is
        // shaded too; nothing reads its mask.
        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 1;
        rt.m_colorFormats[0]      = kShadowMaskFormat;

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

    void ShadowProjectionPass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "ShadowProjectionPass")
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
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();

                const uint32_t sliceCount = ShadowMaskSliceCount(rhiCtx);
                if (sliceCount == 0)
                {
                    return;
                }

                const auto size = p.GetRenderSize();
                p.CreateImage(RHI::AttachmentId("ShadowMask"), RHI::ImageDescriptor::Create2DArray(
                    RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead,
                    size.x, size.y, static_cast<uint16_t>(sliceCount), kShadowMaskFormat));

                RHI::AttachmentLoadStoreAction clear;
                clear.m_clearValue  = RHI::ClearValue::CreateVector4Float(1.f, 1.f, 1.f, 1.f);
                clear.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                clear.m_storeAction = RHI::AttachmentStoreAction::Store;

                // A one-slice array is not an array as far as view creation is concerned
                // unless the view says so, and one to four shadowed lights is one slice.
                RHI::ImageViewDescriptor maskView;
                maskView.m_isArray = 1;

                auto s = p.Scope();
                s.RenderTarget(RHI::AttachmentId("ShadowMask"), clear).View(maskView);

                // Both depth resources are typeless underneath, read as R32_FLOAT.
                s.Read(RHI::AttachmentId("GBufferNormal")).Bind(RHI::InputName("g_GBufferNormal"));
                s.Read(RHI::AttachmentId("SceneDepth")).Format(RHI::Format::R32_FLOAT).Bind(RHI::InputName("g_Depth"));
                s.Read(RHI::AttachmentId("ShadowAtlas")).Format(RHI::Format::R32_FLOAT).Bind(RHI::InputName("g_ShadowAtlas"));

                RHI::SamplerState shadowSampler = RHI::SamplerState::Create(
                    RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp);
                shadowSampler.m_reductionType  = RHI::ReductionType::Comparison;
                // Reversed-Z: lit when the receiver is at or nearer than the stored occluder.
                shadowSampler.m_comparisonFunc = RHI::ComparisonFunc::GreaterEqual;
                s.Sampler(RHI::InputName("g_ShadowSampler"), shadowSampler);

                // With no shadowed light there is no draw, but the mask still exists at one
                // slice: it clears to 1, which already reads as fully lit, and keeping it bound
                // every frame spares the lighting pass a sometimes-bound texture.
                const uint32_t shadowedSlices = ShadowedSliceCount(rhiCtx);
                if (shadowedSlices > 0)
                {
                    // Full-screen triangle, one instance per slice.
                    s.Draw(RHI::DrawLinear(3, 0), shadowedSlices);
                }
            })
            .Finalize()
        ;
    }
}
