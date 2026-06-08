#include "DepthPrePass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>

#include <Pass/PassContext.h>
#include <Pass/PassBuilder.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <Resource/AssetManagerInterface.h>

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
        rt.m_depthStencilFormat = RHI::Format::D32_FLOAT;

        RHI::InputStreamLayout input;
        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        builder.AddBuffer()->Channel("POSITION", 0, Spark::RHI::Format::R32G32B32_FLOAT);
        input = builder.End();

        RHI::RenderStates states;
        states.m_depthStencilState.m_depth.m_enable    = 1;
        states.m_depthStencilState.m_depth.m_writeMask = Spark::RHI::DepthWriteMask::All;
        states.m_depthStencilState.m_depth.m_func      = Spark::RHI::ComparisonFunc::Less;
        states.m_depthStencilState.m_stencil.m_enable  = 0;
        states.m_rasterState.m_cullMode                = Spark::RHI::CullMode::Back;

        RenderPassConfig cfg;
        cfg.m_vertexShader       = shaderAsset;
        cfg.m_renderTargetLayout = rt;
        cfg.m_inputLayout        = input;
        cfg.m_renderStates       = states;

        return cfg;
    }

    void DepthPrePass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "DepthPrePass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(cfg.m_vertexShader)
            .InputLayout(cfg.m_inputLayout)
            .RenderTargetLayout(cfg.m_renderTargetLayout)
            .RenderStates(cfg.m_renderStates)
            .ViewportScissor(cfg.m_viewport, cfg.m_scissor)
            .Build([&](RenderGraphBuilder& builder)
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

            })
            .Execute([](ExecuteWork& work, RenderGraphExecuter&)
            {
                auto& rhi = *RHI::RHIExecuteContext::Current();
                rhi.GetView<SPARK_PASS_TAG("DepthPrePass"), RHI::DrawItem>().each(
                [&](RHI::RHIHandle, const RHI::DrawItem& item) 
                {
                    work.m_commandList->Submit(item); 
                });
            });
        ;
    }
}