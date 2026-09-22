#include "ShadowProjectionPass.h"

#include <EASTL/algorithm.h>

#include <CoreComponents/Tags.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassAccess.h>
#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/RenderPass.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphCompiler.h>
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
        constexpr const char* s_normalSlot = "GBufferNormal";
        constexpr const char* s_depthSlot  = "SceneDepth";
        constexpr const char* s_atlasSlot  = "ShadowAtlasRead";

        Render::ImageAttachmentBindInfo ShaderReadBind(const char* slot, bool asDepth)
        {
            Render::ImageAttachmentBindInfo bind;
            bind.m_slot  = RHI::InputName(slot);
            bind.m_usage = RHI::AttachmentUsage::Shader;
            bind.m_stage = RHI::AttachmentStage::FragmentShader;
            if (asDepth)
            {
                // Both depth resources are typeless underneath; a ShaderRead-only R32_FLOAT
                // view keeps ImageView init from also building a DSV at that format.
                bind.m_view.m_overrideFormat    = RHI::Format::R32_FLOAT;
                bind.m_view.m_overrideBindFlags = RHI::ImageBindFlags::ShaderRead;
            }
            bind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
            bind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;
            return bind;
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

        for (auto [entity, layout] : ctx.GetView<ShadowMaskDrawTag, ShadowMaskLayout>().each())
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
            .Build([](RenderGraphBuilder& builder)
            {
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();

                const uint32_t sliceCount = ShadowMaskSliceCount(rhiCtx);
                if (sliceCount == 0)
                {
                    return;
                }

                const auto size = builder.GetRenderSize();
                auto desc = RHI::ImageDescriptor::Create2DArray(
                    RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead,
                    size.x, size.y, static_cast<uint16_t>(sliceCount), kShadowMaskFormat);

                Render::ImageAttachmentBindInfo maskBind;
                maskBind.m_slot  = RHI::InputName("ShadowMask");
                maskBind.m_usage = RHI::AttachmentUsage::RenderTarget;
                maskBind.m_stage = RHI::AttachmentStage::ColorAttachmentOutput;
                // A one-slice array is not an array as far as view creation is concerned
                // unless the view says so, and one to four shadowed lights is one slice.
                maskBind.m_view.m_isArray = 1;
                maskBind.m_action.m_clearValue  = RHI::ClearValue::CreateVector4Float(1.f, 1.f, 1.f, 1.f);
                maskBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                maskBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.CreateImageAttachment<SPARK_PASS_TAG("ShadowProjectionPass")>(
                    RHI::AttachmentId("ShadowMask"), desc, maskBind, RHI::AttachmentAccess::Write);

                builder.ReadImageAttachment<SPARK_PASS_TAG("ShadowProjectionPass")>(
                    RHI::AttachmentId(s_normalSlot), ShaderReadBind(s_normalSlot, false));
                builder.ReadImageAttachment<SPARK_PASS_TAG("ShadowProjectionPass")>(
                    RHI::AttachmentId(s_depthSlot), ShaderReadBind(s_depthSlot, true));

                Render::ImageAttachmentBindInfo atlasBind = ShaderReadBind(s_atlasSlot, true);
                builder.ReadImageAttachment<SPARK_PASS_TAG("ShadowProjectionPass")>(
                    RHI::AttachmentId("ShadowAtlas"), atlasBind);
            })
            .Compile([](RenderGraphCompiler& compiler)
            {
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();
                const uint32_t frameIndex = compiler.GetFrameIndex();

                struct Input { const char* m_slot; const char* m_name; };
                constexpr Input inputs[] = {
                    { s_normalSlot, "g_GBufferNormal" },
                    { s_depthSlot,  "g_Depth"         },
                    { s_atlasSlot,  "g_ShadowAtlas"   },
                };
                for (const Input& in : inputs)
                {
                    if (RHI::ImageView* view = FindPassAttachmentImageView<SPARK_PASS_TAG("ShadowProjectionPass")>(
                            rhiCtx, RHI::InputName(in.m_slot), frameIndex))
                    {
                        SetPassShaderImage<SPARK_PASS_TAG("ShadowProjectionPass")>(
                            kPerPassSpaceId, RHI::InputName(in.m_name), view);
                    }
                }

                RHI::SamplerState shadowSampler = RHI::SamplerState::Create(
                    RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp);
                shadowSampler.m_reductionType  = RHI::ReductionType::Comparison;
                // Reversed-Z: lit when the receiver is at or nearer than the stored occluder.
                shadowSampler.m_comparisonFunc = RHI::ComparisonFunc::GreaterEqual;
                SetPassShaderSampler<SPARK_PASS_TAG("ShadowProjectionPass")>(
                    kPerPassSpaceId, RHI::InputName("g_ShadowSampler"), shadowSampler);
            })
            .Finalize()
        ;
    }
}
