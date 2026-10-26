#include "TranslucencyPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/RenderPass.h>

#include <Feature/InstanceLists/InstanceLists.h>
#include <Binding/Scene/SceneBinding.h>
#include <Binding/View/ViewBinding.h>
#include <View/ShadowAtlasAllocator.h>
#include <View/ViewTags.h>
#include <Binding/Material/MaterialBinding.h>
#include <Binding/Instance/InstanceBinding.h>
#include <Geometry/MeshGeometry.h>
#include <Drawable/DrawTag.h>
#include <DrawList/DrawList.h>

#include <RenderGraph/RenderGraphBuilder.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    RenderPassConfig TranslucencyPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/Translucency/Translucency.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[TranslucencyPass] Failed to load shader Translucency.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 1;
        rt.m_colorFormats[0]      = RHI::Format::R16G16B16A16_FLOAT;  // SceneColor
        rt.m_depthStencilFormat   = RHI::Format::D32_FLOAT;

        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        builder.AddBuffer()
               ->Channel("POSITION", 0, RHI::Format::R32G32B32_FLOAT)
               ->Channel("NORMAL",   0, RHI::Format::R32G32B32_FLOAT)
               ->Channel("TANGENT",  0, RHI::Format::R32G32B32A32_FLOAT)
               ->Channel("TEXCOORD", 0, RHI::Format::R32G32_FLOAT);
        builder.AddBuffer(RHI::StreamStepFunction::PerInstance, 1)
               ->Channel("INSTANCE_INDEX", 0, RHI::Format::R32_UINT);
        RHI::InputStreamLayout input = builder.End();

        RHI::RenderStates states;
        // Hidden behind opaque depth, never hiding what is drawn after it: tested against
        // SceneDepth (reversed-Z, nearer is greater), not written.
        states.m_depthStencilState.m_depth.m_enable    = 1;
        states.m_depthStencilState.m_depth.m_writeMask = RHI::DepthWriteMask::Zero;
        states.m_depthStencilState.m_depth.m_func      = RHI::ComparisonFunc::GreaterEqual;
        states.m_depthStencilState.m_stencil.m_enable  = 0;
        states.m_rasterState.m_cullMode                = RHI::CullMode::Back;

        // Over what is behind by the surface's opacity; SceneColor's alpha is kept, as the
        // lighting passes keep it.
        auto& blend = states.m_blendState.m_targets[0];
        blend.m_enable           = 1;
        blend.m_blendSource      = RHI::BlendFactor::AlphaSource;
        blend.m_blendDest        = RHI::BlendFactor::AlphaSourceInverse;
        blend.m_blendOp          = RHI::BlendOp::Add;
        blend.m_blendAlphaSource = RHI::BlendFactor::Zero;
        blend.m_blendAlphaDest   = RHI::BlendFactor::One;
        blend.m_blendAlphaOp     = RHI::BlendOp::Add;

        RenderPassConfig cfg;
        cfg.m_vertexShader       = shaderAsset;
        cfg.m_fragmentShader     = shaderAsset;
        cfg.m_renderTargetLayout = rt;
        cfg.m_inputLayout        = input;
        cfg.m_renderStates       = states;
        return cfg;
    }

    void TranslucencyPass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "TranslucencyPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(cfg.m_vertexShader)
            .FragmentShader(cfg.m_fragmentShader)
            .InputLayout(cfg.m_inputLayout)
            .RenderTargetLayout(cfg.m_renderTargetLayout)
            .RenderStates(cfg.m_renderStates)
            .Binds<ViewBindingTag, MainSceneTag, MaterialBindingTag, InstanceBindingTag>()
            .RendersView<MainViewTag>()
            .Draw<TranslucentTag>()
            .Build([](RenderPassScopes& p)
            {
                RHI::AttachmentLoadStoreAction load;
                load.m_loadAction  = RHI::AttachmentLoadAction::Load;
                load.m_storeAction = RHI::AttachmentStoreAction::Store;

                auto s = p.Scope();
                s.RenderTarget(RHI::AttachmentId("SceneColor"), load);
                s.DepthRead(RHI::AttachmentId("SceneDepth"));
                s.Sampler(RHI::InputName("g_MatSampler"),
                    RHI::SamplerState::CreateAnisotropic(16, RHI::AddressMode::Wrap));

                // Only lights granted tiles sample it, and none is until it exists.
                if (IsShadowAtlasReady(*RHI::RHIExecuteContext::Current()))
                {
                    s.ReadImage(RHI::AttachmentId("ShadowAtlas")).Format(RHI::Format::R32_FLOAT)
                        .Bind(RHI::InputName("g_ShadowAtlas"));
                    s.Sampler(RHI::InputName("g_ShadowSampler"), ShadowAtlasSampler());
                }

                // The list's draws, once DrawListCullingSystem has made their buffer and the
                // geometry, the instance IDs and the instances exist.
                const RHI::AttachmentId& arguments = InstanceLists::CpuCulled::DrawArgumentsName();
                if (p.Import<CpuCulledDrawArgumentsTag>(arguments)
                    && p.Import<MeshGeometryPoolTag>(RHI::AttachmentId("MeshGeometry"))
                    && p.Import<InstanceIDBufferTag>(RHI::AttachmentId("InstanceIDs"))
                    && p.Import<InstancePoolTag>(RHI::AttachmentId("Instances")))
                {
                    // The pool is the first vertex stream and the indices, the instance IDs the
                    // second. Every mesh's indices are 32 bit: the model loader writes no other.
                    const Attachment geometry   = s.InputAssembly(RHI::AttachmentId("MeshGeometry"));
                    const Attachment vertices[] = { geometry, s.InputAssembly(RHI::AttachmentId("InstanceIDs")) };
                    // Through the shared binding, by the vertex shader.
                    s.ReadBuffer(RHI::AttachmentId("Instances")).Stage(RHI::AttachmentStage::VertexShader);
                    s.DrawIndirect(arguments, vertices, IndexStream{ geometry, RHI::IndexFormat::UINT32 });
                }
                s.Close();
            })
            .Finalize()
        ;
    }
}
