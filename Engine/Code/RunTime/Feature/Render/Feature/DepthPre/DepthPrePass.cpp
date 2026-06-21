#include "DepthPrePass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>

#include <Pass/PassContext.h>
#include <Pass/RenderPass.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <Resource/AssetManagerInterface.h>

#include "../../../UI/UIBaseSystem.h"

namespace Spark::Render
{
    RenderPassConfig DepthPrePass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("Shaders/DepthPre/DepthPre.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[DepthPrePass] Failed to loaded shader DepthPre.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 1;
        rt.m_colorFormats[0] = RHI::Format::R8G8B8A8_UNORM;
        rt.m_depthStencilFormat = RHI::Format::D32_FLOAT;

        RHI::InputStreamLayout input;
        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        builder.AddBuffer()->Channel("POSITION", 0, Spark::RHI::Format::R32G32B32_FLOAT);
                           //->Padding(48);
        input = builder.End();

        RHI::RenderStates states;
        states.m_depthStencilState.m_depth.m_enable    = 1;
        states.m_depthStencilState.m_depth.m_writeMask = Spark::RHI::DepthWriteMask::All;
        states.m_depthStencilState.m_depth.m_func      = Spark::RHI::ComparisonFunc::Less;
        states.m_depthStencilState.m_stencil.m_enable  = 0;
        states.m_rasterState.m_cullMode                = Spark::RHI::CullMode::Back;

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

    void DepthPrePass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "DepthPrePass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(cfg.m_vertexShader)
            .FragmentShader(cfg.m_fragmentShader)
            .InputLayout(cfg.m_inputLayout)
            .RenderTargetLayout(cfg.m_renderTargetLayout)
            .RenderStates(cfg.m_renderStates)
            .ViewportScissor(cfg.m_viewport, cfg.m_scissor)
            .Build([&, cfg](RenderGraphBuilder& builder)
            {
                auto depthDesc = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::DepthStencil | RHI::ImageBindFlags::ShaderRead,
                    builder.GetRenderSize().x, 
                    builder.GetRenderSize().y, 
                    cfg.m_renderTargetLayout.m_depthStencilFormat);
                depthDesc.m_multisampleState = cfg.m_multisampleState;
                
                Render::ImageAttachmentBindInfo bind;
                bind.m_slot  = RHI::InputName("OutputDepth");
                bind.m_usage = RHI::AttachmentUsage::DepthStencil;
                bind.m_stage = RHI::AttachmentStage::EarlyFragmentTest | RHI::AttachmentStage::LateFragmentTest;
                bind.m_action.m_clearValue  = RHI::ClearValue::CreateDepth(1.0f);
                bind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                bind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.CreateImageAttachment<SPARK_PASS_TAG("DepthPrePass")>(
                    RHI::AttachmentId("SceneDepth"),
                    depthDesc,
                    bind,
                    RHI::AttachmentAccess::Write
                );

                auto colorDesc = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyRead,
                    builder.GetRenderSize().x,
                    builder.GetRenderSize().y,
                    RHI::Format::R8G8B8A8_UNORM);

                Render::ImageAttachmentBindInfo colorBind;
                colorBind.m_slot  = RHI::InputName("SceneColor");
                colorBind.m_usage = RHI::AttachmentUsage::RenderTarget;
                colorBind.m_stage = RHI::AttachmentStage::ColorAttachmentOutput;
                colorBind.m_action.m_clearValue  = RHI::ClearValue::CreateVector4Float(0.1f, 0.1f, 0.15f, 1.f);
                colorBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                colorBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.CreateImageAttachment<SPARK_PASS_TAG("DepthPrePass")>(
                    RHI::AttachmentId("SceneColor"),
                    colorDesc,
                    colorBind,
                    RHI::AttachmentAccess::Write
                );

            })
            .Execute([](ExecuteWork& work, RenderGraphExecuter&)
            {
                auto& rhi = *RHI::RHIExecuteContext::Current();
                rhi.GetView<SPARK_PASS_TAG("DepthPrePass"), RHI::DrawItem>().each(
                [&](RHI::RHIHandle, const RHI::DrawItem& item) 
                {
                    work.m_commandList->Submit(item); 
                });
            })
            .Finalize()
            ;
        ;
    }
}