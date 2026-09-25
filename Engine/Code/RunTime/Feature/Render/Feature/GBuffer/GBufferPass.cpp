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
#include <Binding/Instance/InstanceBinding.h>

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

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/GBuffer/GBuffer.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[GBufferPass] Failed to load shader GBuffer.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // Five color targets + read-only SceneDepth. The depth format must match the
        // SceneDepth attachment DepthPrePass creates (D32_FLOAT). Lighting reconstructs
        // world position from SceneDepth, so no position target is written.
        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 5;
        // Layout and channel assignment: Shaders/Lib/DeferredShadingCommon.hlsli.
        rt.m_colorFormats[0]      = RHI::Format::R16G16B16A16_FLOAT;  // SceneColor (emissive)
        rt.m_colorFormats[1]      = RHI::Format::R10G10B10A2_UNORM;   // GBufferNormal
        rt.m_colorFormats[2]      = RHI::Format::R8G8B8A8_UNORM;      // GBufferSurface
        rt.m_colorFormats[3]      = RHI::Format::R8G8B8A8_UNORM_SRGB; // GBufferBaseColor
        rt.m_colorFormats[4]      = RHI::Format::R16G16_FLOAT;        // Velocity (NDC)
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
            .Binds<MaterialBindingTag, InstanceBindingTag>()
            .RendersView<MainViewTag>()
            .BuildScopes([](RenderPassScopes& p)
            {
                const auto size = p.GetRenderSize();
                auto s = p.Scope();

                // Transient GBuffer color target: cleared + stored, and shader-readable
                // so the deferred lighting pass can sample it. Numbered in the order declared.
                auto createColor = [&](const char* name, RHI::Format fmt, const RHI::ClearValue& clearValue)
                {
                    p.CreateImage(RHI::AttachmentId(name), RHI::ImageDescriptor::Create2D(
                        RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead,
                        size.x, size.y, fmt));

                    RHI::AttachmentLoadStoreAction clear;
                    clear.m_clearValue  = clearValue;
                    clear.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                    clear.m_storeAction = RHI::AttachmentStoreAction::Store;
                    s.RenderTarget(RHI::AttachmentId(name), clear);
                };

                // This pass owns SceneColor: it is the first to produce scene radiance
                // (the material's emissive), and every lighting pass after it blends on
                // top. The clear happens at BeginRenderPass whatever the depth test does,
                // so sky pixels keep it for the skybox to overwrite.
                //
                // The clear is the one SceneColor write not scaled by PreExposure. It only
                // shows with no skybox, and PreExposure is 1 today -- but it has to be
                // scaled too once P3's EyeAdaptation drives it, or the fallback background
                // comes out wrong.
                createColor("SceneColor", RHI::Format::R16G16B16A16_FLOAT,
                    RHI::ClearValue::CreateVector4Float(0.1f, 0.1f, 0.15f, 1.f));

                // Every pass that reads these depth-tests the sky away, so the clear values
                // only have to decode to something sane in a capture: +Z normal, Unlit,
                // black unoccluded base color.
                createColor("GBufferNormal", RHI::Format::R10G10B10A2_UNORM,
                    RHI::ClearValue::CreateVector4Float(0.5f, 0.5f, 1.f, 0.f));
                createColor("GBufferSurface", RHI::Format::R8G8B8A8_UNORM,
                    RHI::ClearValue::CreateVector4Float(0.f, 0.5f, 1.f, 0.f));
                createColor("GBufferBaseColor", RHI::Format::R8G8B8A8_UNORM_SRGB,
                    RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 1.f));
                // kVelocityUnwritten in Lib/Velocity.hlsli: marks pixels no geometry covered.
                createColor("Velocity", RHI::Format::R16G16_FLOAT,
                    RHI::ClearValue::CreateVector4Float(65504.f, 65504.f, 0.f, 0.f));

                // Read-only depth test against DepthPrePass's SceneDepth: Load the depth
                // to test against, Store it back unchanged. A depth read selects a
                // READ_ONLY_DEPTH DSV, so with the Equal / no-write state this pass never
                // modifies depth. Stencil actions default to None (D32_FLOAT has no stencil
                // plane), keeping the DSV in DEPTH_READ.
                s.DepthRead(RHI::AttachmentId("SceneDepth"));

                // Constant material sampler (linear/wrap). Change-detected, so this per-frame
                // set is a no-op after the first bind.
                s.Sampler(RHI::InputName("g_MatSampler"),
                    RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Wrap));

                s.Accepts<OpaqueTag>();
            })
            .Finalize()
        ;
    }
}
