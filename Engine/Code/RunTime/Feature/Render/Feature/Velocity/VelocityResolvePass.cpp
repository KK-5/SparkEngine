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
            .Binds<>()
            .RendersView<MainViewTag>()
            .Build([](RenderPassScopes& p)
            {
                const auto size = p.GetRenderSize();
                p.CreateImage(RHI::AttachmentId("ResolvedVelocity"), RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead,
                    size.x, size.y, RHI::Format::R16G16_FLOAT));

                // Cleared so a warmup frame whose draw is dropped still reads as no motion.
                RHI::AttachmentLoadStoreAction clear;
                clear.m_clearValue  = RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 0.f);
                clear.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                clear.m_storeAction = RHI::AttachmentStoreAction::Store;

                auto s = p.Scope();
                s.RenderTarget(RHI::AttachmentId("ResolvedVelocity"), clear);
                s.Read(RHI::AttachmentId(s_velocitySlot)).Bind(RHI::InputName(s_velocityInput));
                // Same R32_FLOAT shader-read view over the typeless depth as LightingPass.
                s.Read(RHI::AttachmentId(s_depthSlot)).Format(RHI::Format::R32_FLOAT).Bind(RHI::InputName(s_depthInput));

                s.Draw(RHI::DrawLinear(3, 0)); // full-screen triangle
            })
            .Finalize()
        ;
    }
}
