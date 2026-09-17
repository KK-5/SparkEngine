#include "VelocityResolvePass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/RenderPass.h>
#include <Pass/PassAccess.h>

#include <Drawable/DrawTag.h>    // FullScreenTriangleTag

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphCompiler.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <View/ViewTags.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        constexpr const char* s_velocitySlot  = "Velocity";
        constexpr const char* s_velocityInput = "g_Velocity";
        constexpr const char* s_depthSlot     = "SceneDepth";
        constexpr const char* s_depthInput    = "g_Depth";
    }

    RenderPassConfig VelocityResolvePass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/Velocity/VelocityResolve.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[VelocityResolvePass] Failed to load shader VelocityResolve.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // Every pixel is written, so no depth test and no depth attachment.
        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 1;
        rt.m_colorFormats[0]      = RHI::Format::R16G16_FLOAT;

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

    void VelocityResolvePass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "VelocityResolvePass")
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
                const auto size = builder.GetRenderSize();

                auto desc = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead,
                    size.x, size.y, RHI::Format::R16G16_FLOAT);

                // Cleared so a warmup frame whose draw is dropped still reads as no motion.
                Render::ImageAttachmentBindInfo outputBind;
                outputBind.m_slot  = RHI::InputName("ResolvedVelocity");
                outputBind.m_usage = RHI::AttachmentUsage::RenderTarget;
                outputBind.m_stage = RHI::AttachmentStage::ColorAttachmentOutput;
                outputBind.m_action.m_clearValue  = RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 0.f);
                outputBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                outputBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.CreateImageAttachment<SPARK_PASS_TAG("VelocityResolvePass")>(
                    RHI::AttachmentId("ResolvedVelocity"), desc, outputBind, RHI::AttachmentAccess::Write);

                Render::ImageAttachmentBindInfo velocityBind;
                velocityBind.m_slot  = RHI::InputName(s_velocitySlot);
                velocityBind.m_usage = RHI::AttachmentUsage::Shader;
                velocityBind.m_stage = RHI::AttachmentStage::FragmentShader;
                velocityBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                velocityBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.ReadImageAttachment<SPARK_PASS_TAG("VelocityResolvePass")>(
                    RHI::AttachmentId(s_velocitySlot), velocityBind);

                // Same R32_FLOAT shader-read view over the typeless depth as LightingPass.
                Render::ImageAttachmentBindInfo depthBind;
                depthBind.m_slot  = RHI::InputName(s_depthSlot);
                depthBind.m_usage = RHI::AttachmentUsage::Shader;
                depthBind.m_stage = RHI::AttachmentStage::FragmentShader;
                depthBind.m_view.m_overrideFormat    = RHI::Format::R32_FLOAT;
                depthBind.m_view.m_overrideBindFlags = RHI::ImageBindFlags::ShaderRead;
                depthBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                depthBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.ReadImageAttachment<SPARK_PASS_TAG("VelocityResolvePass")>(
                    RHI::AttachmentId(s_depthSlot), depthBind);
            })
            .Compile([](RenderGraphCompiler& compiler)
            {
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();
                const uint32_t frameIndex = compiler.GetFrameIndex();

                if (RHI::ImageView* view = FindPassAttachmentImageView<SPARK_PASS_TAG("VelocityResolvePass")>(
                        rhiCtx, RHI::InputName(s_velocitySlot), frameIndex))
                {
                    SetPassShaderImage<SPARK_PASS_TAG("VelocityResolvePass")>(
                        kPerPassSpaceId, RHI::InputName(s_velocityInput), view);
                }
                if (RHI::ImageView* view = FindPassAttachmentImageView<SPARK_PASS_TAG("VelocityResolvePass")>(
                        rhiCtx, RHI::InputName(s_depthSlot), frameIndex))
                {
                    SetPassShaderImage<SPARK_PASS_TAG("VelocityResolvePass")>(
                        kPerPassSpaceId, RHI::InputName(s_depthInput), view);
                }
            })
            .Finalize()
        ;
    }
}
