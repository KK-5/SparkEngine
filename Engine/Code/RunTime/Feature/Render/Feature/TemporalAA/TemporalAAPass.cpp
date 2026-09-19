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
#include <Pass/PassAccess.h>

#include <Drawable/DrawTag.h>    // FullScreenTriangleTag

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphCompiler.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <View/ViewTags.h>
#include <View/ViewComponents.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        constexpr const char* s_outputName    = "TemporalAA";
        constexpr const char* s_historySlot   = "History";
        constexpr const char* s_colorSlot     = "SceneColor";
        constexpr const char* s_depthSlot     = "SceneDepth";
        constexpr const char* s_velocitySlot  = "ResolvedVelocity";

        struct ShaderInput
        {
            const char* m_slot;
            const char* m_input;
        };

        constexpr ShaderInput s_shaderImages[] = {
            { s_colorSlot,    "g_SceneColor" },
            { s_depthSlot,    "g_Depth" },
            { s_velocitySlot, "g_Velocity" },
            { s_historySlot,  "g_History" },
        };

        Render::ImageAttachmentBindInfo ShaderReadBind(const char* slot)
        {
            Render::ImageAttachmentBindInfo bind;
            bind.m_slot  = RHI::InputName(slot);
            bind.m_usage = RHI::AttachmentUsage::Shader;
            bind.m_stage = RHI::AttachmentStage::FragmentShader;
            bind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
            bind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;
            return bind;
        }
    }

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
            .Accepts<FullScreenTriangleTag>()
            .Binds<>()
            .RendersView<MainViewTag>()
            .Build([](RenderGraphBuilder& builder)
            {
                // Declaring nothing leaves the pass without a RenderPassBeginInfo, which the
                // executer skips.
                if (!FindMainViewSettings(*RHI::RHIExecuteContext::Current()))
                {
                    return;
                }

                const auto size = builder.GetRenderSize();
                auto desc = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead,
                    size.x, size.y, RHI::Format::R16G16B16A16_FLOAT);

                Render::ImageAttachmentBindInfo outputBind;
                outputBind.m_slot  = RHI::InputName(s_outputName);
                outputBind.m_usage = RHI::AttachmentUsage::RenderTarget;
                outputBind.m_stage = RHI::AttachmentStage::ColorAttachmentOutput;
                outputBind.m_action.m_clearValue  = RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 0.f);
                outputBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                outputBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.CreateImageAttachment<SPARK_PASS_TAG("TemporalAAPass")>(
                    RHI::AttachmentId(s_outputName), desc, outputBind, RHI::AttachmentAccess::Write);

                // After the Create: a previous-frame read needs this frame's name declared.
                builder.ReadPreviousImageAttachment<SPARK_PASS_TAG("TemporalAAPass")>(
                    RHI::AttachmentId(s_outputName), ShaderReadBind(s_historySlot));

                builder.ReadImageAttachment<SPARK_PASS_TAG("TemporalAAPass")>(
                    RHI::AttachmentId(s_colorSlot), ShaderReadBind(s_colorSlot));

                builder.ReadImageAttachment<SPARK_PASS_TAG("TemporalAAPass")>(
                    RHI::AttachmentId(s_velocitySlot), ShaderReadBind(s_velocitySlot));

                // Same R32_FLOAT shader-read view over the typeless depth as LightingPass.
                Render::ImageAttachmentBindInfo depthBind = ShaderReadBind(s_depthSlot);
                depthBind.m_view.m_overrideFormat    = RHI::Format::R32_FLOAT;
                depthBind.m_view.m_overrideBindFlags = RHI::ImageBindFlags::ShaderRead;
                builder.ReadImageAttachment<SPARK_PASS_TAG("TemporalAAPass")>(
                    RHI::AttachmentId(s_depthSlot), depthBind);
            })
            .Compile([](RenderGraphCompiler& compiler)
            {
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();
                const ViewTemporalAA* settings = FindMainViewSettings(rhiCtx);
                if (!settings)
                {
                    return;
                }

                const uint32_t frameIndex = compiler.GetFrameIndex();
                for (const ShaderInput& image : s_shaderImages)
                {
                    if (RHI::ImageView* view = FindPassAttachmentImageView<SPARK_PASS_TAG("TemporalAAPass")>(
                            rhiCtx, RHI::InputName(image.m_slot), frameIndex))
                    {
                        SetPassShaderImage<SPARK_PASS_TAG("TemporalAAPass")>(
                            kPerPassSpaceId, RHI::InputName(image.m_input), view);
                    }
                }

                SetPassShaderSampler<SPARK_PASS_TAG("TemporalAAPass")>(
                    kPerPassSpaceId, RHI::InputName("g_LinearSampler"),
                    RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp));

                const uint32_t historyValid = IsPreviousFrameMissing<SPARK_PASS_TAG("TemporalAAPass")>(
                    rhiCtx, RHI::InputName(s_historySlot)) ? 0u : 1u;
                SetPassShaderConstant<SPARK_PASS_TAG("TemporalAAPass")>(
                    kPerPassSpaceId, RHI::InputName("g_TemporalAAHistoryValid"), historyValid);

                SetPassShaderConstant<SPARK_PASS_TAG("TemporalAAPass")>(
                    kPerPassSpaceId, RHI::InputName("g_TemporalAACurrentFrameWeight"), settings->m_currentFrameWeight);
                SetPassShaderConstant<SPARK_PASS_TAG("TemporalAAPass")>(
                    kPerPassSpaceId, RHI::InputName("g_TemporalAAMotionFrameWeight"), settings->m_motionFrameWeight);
                SetPassShaderConstant<SPARK_PASS_TAG("TemporalAAPass")>(
                    kPerPassSpaceId, RHI::InputName("g_TemporalAAVarianceClipGamma"), settings->m_varianceClipGamma);
                SetPassShaderConstant<SPARK_PASS_TAG("TemporalAAPass")>(
                    kPerPassSpaceId, RHI::InputName("g_TemporalAAFilterSize"), settings->m_filterSize);
            })
            .Finalize()
        ;
    }

    const ViewTemporalAA* TemporalAAPass::FindMainViewSettings(RHI::RHIContext& ctx)
    {
        for (auto [view, settings] : ctx.GetView<MainViewTag, ViewTemporalAA>().each())
        {
            return &settings;
        }
        return nullptr;
    }
}
