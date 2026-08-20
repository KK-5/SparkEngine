#include "TonemapPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
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
    RenderPassConfig TonemapPass::DefaultConfig()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("Shaders/Tonemap/Tonemap.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[TonemapPass] Failed to load shader Tonemap.hlsl.");
            return {};
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // Color-only pass writing the LDR swap chain — no depth. The full-screen triangle
        // covers every pixel, so there is no depth test and no depth attachment
        // (m_depthStencilFormat stays Format::Unknown). The color format must match the
        // swap chain (R8G8B8A8_UNORM, see RenderSystem::InitRHIData).
        RHI::RenderTargetLayout rt;
        rt.m_colorAttachmentCount = 1;
        rt.m_colorFormats[0]      = RHI::Format::R8G8B8A8_UNORM;

        // Empty input layout: the full-screen triangle is generated from SV_VertexID.
        RHI::InputStreamLayoutBuilder builder;
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        RHI::InputStreamLayout input = builder.End();

        RHI::RenderStates states;
        states.m_depthStencilState.m_depth.m_enable   = 0;
        states.m_depthStencilState.m_stencil.m_enable = 0;
        states.m_rasterState.m_cullMode               = RHI::CullMode::None; // full-screen triangle

        RenderPassConfig cfg;
        cfg.m_vertexShader       = shaderAsset;
        cfg.m_fragmentShader     = shaderAsset;
        cfg.m_renderTargetLayout = rt;
        cfg.m_inputLayout        = input;
        cfg.m_renderStates       = states;


        return cfg;
    }

    void TonemapPass::SetUp(PassContext& ctx, const RenderPassConfig& cfg)
    {
        SPARK_RENDER_PASS(ctx, "TonemapPass")
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
                // Import the swap chain as this pass's color render target. TonemapPass is
                // now the first pass to touch the swap chain (CopyFrameBufferPass is no
                // longer in the pipeline), so it owns the import; UIPass writes the same
                // imported "SwapChain" attachment afterwards. Clear (black) so the warmup
                // frame — before SceneColor is ready and the draw is dropped — is not garbage.
                Render::ImportedImageAttachmentBindInfo swapBind;
                swapBind.m_slot   = RHI::InputName("ColorOutput");
                swapBind.m_usage  = RHI::AttachmentUsage::RenderTarget;
                swapBind.m_stage  = RHI::AttachmentStage::ColorAttachmentOutput;
                swapBind.m_access = RHI::AttachmentAccess::Write;
                swapBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                swapBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;
                swapBind.m_action.m_clearValue  = RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 1.f);
                swapBind.m_image  = builder.GetCurrentSwapChainResource();

                builder.ImportImageAttachment<SPARK_PASS_TAG("TonemapPass")>(
                    RHI::AttachmentId("SwapChain"), swapBind);

                // Read the HDR SceneColor as a shader resource. Declaring it here orders
                // this pass after the lighting/skybox writers and transitions SceneColor
                // from RenderTarget to shader-read before this pass runs. The view→SRG
                // binding happens in the Compile hook below.
                Render::ImageAttachmentBindInfo readBind;
                readBind.m_slot  = RHI::InputName("SceneColor");
                readBind.m_usage = RHI::AttachmentUsage::Shader;
                readBind.m_stage = RHI::AttachmentStage::FragmentShader;
                readBind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Load;
                readBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.ReadImageAttachment<SPARK_PASS_TAG("TonemapPass")>(
                    RHI::AttachmentId("SceneColor"), readBind);
            })
            .Compile([](RenderGraphCompiler& compiler)
            {
                // Post-CompileTransientResources, pre-CompileShaderInputs: SceneColor is
                // materialized, so resolve its view and stage it into this pass's space2
                // (per-pass tier) SRG (auto-created by Finalize). The full-screen triangle
                // DrawItem comes from the shared FullScreenTriangleTag entity.
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();
                const uint32_t frameIndex = compiler.GetFrameIndex();

                RHI::ImageView* sceneColorView = FindPassAttachmentImageView<SPARK_PASS_TAG("TonemapPass")>(
                    rhiCtx, RHI::InputName("SceneColor"), frameIndex);
                if (sceneColorView)
                {
                    SetPassShaderImage<SPARK_PASS_TAG("TonemapPass")>(2, RHI::InputName("g_SceneColor"), sceneColorView);
                }
            })
            .Finalize()
        ;
    }
}
