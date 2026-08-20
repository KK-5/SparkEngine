#include "DepthPrePass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>

#include <Pass/PassContext.h>
#include <Pass/RenderPass.h>

#include <Drawable/DrawTag.h>
#include <View/ViewTags.h>
#include <Binding/Instance/InstanceBinding.h>

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

        Resource::AssetId assetId = assetManager->MakeAssetId("Shaders/DepthOnly/DepthOnly.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[DepthPrePass] Failed to loaded shader DepthOnly.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // Depth-only pass: no color attachment. SceneColor is created and owned by
        // LightingPass (the first pass that produces scene color). NumRenderTargets = 0.
        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 0;
        rt.m_depthStencilFormat = RHI::Format::D32_FLOAT;

        RHI::InputStreamLayout input;
        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        // Depth-only VS reads POSITION alone; it sits at offset 0, so no padding is
        // needed for the trailing NORMAL/TANGENT/TEXCOORD the mesh interleaves.
        builder.AddBuffer()->Channel("POSITION", 0, RHI::Format::R32G32B32_FLOAT);
        builder.AddBuffer(RHI::StreamStepFunction::PerInstance, 1)
               ->Channel("INSTANCE_INDEX", 0, RHI::Format::R32_UINT);
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
            .Accepts<OpaqueTag>()
            .Binds<InstanceBindingTag>()
            .RendersView<MainViewTag>()
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
            })
            .Finalize()
            ;
        ;
    }
}