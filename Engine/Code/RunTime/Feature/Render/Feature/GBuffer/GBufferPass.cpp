#include "GBufferPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/RenderPass.h>
#include <Pass/PassAccess.h>

#include <Drawable/DrawTag.h>
#include <View/ViewTags.h>
#include <Binding/Material/MaterialBinding.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphCompiler.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    RenderPassConfig GBufferPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("Shaders/GBuffer/GBuffer.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[GBufferPass] Failed to load shader GBuffer.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // Three color targets + read-only SceneDepth. The depth format must match the
        // SceneDepth attachment DepthPrePass creates (D32_FLOAT). Lighting reconstructs
        // world position from SceneDepth, so no position target is written.
        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 4;
        rt.m_colorFormats[0]      = RHI::Format::R8G8B8A8_UNORM;      // Albedo
        rt.m_colorFormats[1]      = RHI::Format::R16G16B16A16_FLOAT;  // Normal (world, raw)
        rt.m_colorFormats[2]      = RHI::Format::R8G8B8A8_UNORM;      // ORM
        rt.m_colorFormats[3]      = RHI::Format::R11G11B10_FLOAT;     // Emissive (HDR)
        rt.m_depthStencilFormat   = RHI::Format::D32_FLOAT;

        RHI::InputStreamLayout input;
        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        builder.AddBuffer()
               ->Channel("POSITION", 0, RHI::Format::R32G32B32_FLOAT)
               ->Channel("NORMAL",   0, RHI::Format::R32G32B32_FLOAT)
               ->Channel("TANGENT",  0, RHI::Format::R32G32B32A32_FLOAT)
               ->Channel("TEXCOORD", 0, RHI::Format::R32G32_FLOAT);
        builder.AddBuffer(RHI::StreamStepFunction::PerInstance, 1)
               ->Channel("INSTANCE_INDEX", 0, RHI::Format::R32_UINT);
        input = builder.End();

        RHI::RenderStates states;
        // Depth-equal against DepthPrePass's SceneDepth, never writing depth: the
        // prepass already established the exact visible depth, so Equal keeps only the
        // owning fragment (early-Z, zero overdraw on the GBuffer PS). Read-only DSV is
        // selected by ReadImageAttachment below, not by the write mask.
        states.m_depthStencilState.m_depth.m_enable    = 1;
        states.m_depthStencilState.m_depth.m_writeMask = RHI::DepthWriteMask::Zero;
        states.m_depthStencilState.m_depth.m_func      = RHI::ComparisonFunc::Equal;
        states.m_depthStencilState.m_stencil.m_enable  = 0;
        states.m_rasterState.m_cullMode                = RHI::CullMode::Back;

        RenderPassConfig cfg;
        cfg.m_vertexShader       = shaderAsset;
        cfg.m_fragmentShader     = shaderAsset;
        cfg.m_renderTargetLayout = rt;
        cfg.m_inputLayout        = input;
        cfg.m_renderStates       = states;


        return cfg;
    }

    void GBufferPass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "GBufferPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(cfg.m_vertexShader)
            .FragmentShader(cfg.m_fragmentShader)
            .InputLayout(cfg.m_inputLayout)
            .RenderTargetLayout(cfg.m_renderTargetLayout)
            .RenderStates(cfg.m_renderStates)
            .Accepts<OpaqueTag>()
            .Binds<MaterialBindingTag>()
            .RendersView<MainViewTag>()
            .Build([&, cfg](RenderGraphBuilder& builder)
            {
                const auto size = builder.GetRenderSize();

                // Transient GBuffer color target: cleared + stored, and shader-readable
                // so the deferred lighting pass can sample it.
                auto createColor = [&](const char* id, RHI::Format fmt, const RHI::ClearValue& clear)
                {
                    auto desc = RHI::ImageDescriptor::Create2D(
                        RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead,
                        size.x, size.y, fmt);

                    Render::ImageAttachmentBindInfo bind;
                    bind.m_slot  = RHI::InputName(id);
                    bind.m_usage = RHI::AttachmentUsage::RenderTarget;
                    bind.m_stage = RHI::AttachmentStage::ColorAttachmentOutput;
                    bind.m_action.m_clearValue  = clear;
                    bind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                    bind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                    builder.CreateImageAttachment<SPARK_PASS_TAG("GBufferPass")>(
                        RHI::AttachmentId(id), desc, bind, RHI::AttachmentAccess::Write);
                };

                createColor("GBufferAlbedo", RHI::Format::R8G8B8A8_UNORM,
                    RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 1.f));
                createColor("GBufferNormal", RHI::Format::R16G16B16A16_FLOAT,
                    RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 0.f));
                createColor("GBufferORM", RHI::Format::R8G8B8A8_UNORM,
                    RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 1.f));
                // HDR emissive; cleared to black so non-emissive / sky pixels add nothing.
                createColor("GBufferEmissive", RHI::Format::R11G11B10_FLOAT,
                    RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 0.f));

                // Read-only depth test against DepthPrePass's SceneDepth: Load the depth
                // to test against, Store it back unchanged. ReadImageAttachment selects a
                // READ_ONLY_DEPTH DSV (via AttachmentAccess::Read), so with the Equal /
                // no-write state this pass never modifies depth. Stencil actions default
                // to None (D32_FLOAT has no stencil plane), keeping the DSV in DEPTH_READ.
                RHI::ImageDescriptor depthDesc = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::DepthStencil | RHI::ImageBindFlags::ShaderRead,
                    size.x, size.y, RHI::Format::D32_FLOAT);

                Render::ImageAttachmentBindInfo depthBind;
                depthBind.m_slot  = RHI::InputName("SceneDepth");
                depthBind.m_usage = RHI::AttachmentUsage::DepthStencil;
                depthBind.m_stage = RHI::AttachmentStage::EarlyFragmentTest | RHI::AttachmentStage::LateFragmentTest;
                depthBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                depthBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.ReadImageAttachment<SPARK_PASS_TAG("GBufferPass")>(
                    RHI::AttachmentId("SceneDepth"), depthBind);
            })
            .Compile([](RenderGraphCompiler&)
            {
                // Constant material sampler (linear/wrap) into this pass's space2 bindings
                // (auto-created by Finalize). Change-detected, so this per-frame set is a
                // no-op after the first bind.
                SetPassShaderSampler<SPARK_PASS_TAG("GBufferPass")>(
                    kPerPassSpaceId, RHI::InputName("g_MatSampler"),
                    RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Wrap));
            })
            .Finalize()
        ;
    }
}
