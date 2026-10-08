#include "AmbientOcclusionPass.h"

#include <EASTL/array.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassContext.h>
#include <Pass/ComputePass.h>

#include <RenderGraph/PassScopes.h>

#include <Binding/View/ViewBinding.h>

#include <View/View.h>
#include <View/ViewComponents.h>

#include <Feature/SceneTextures/SceneTextures.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        //! The view-space depth chain, the main pass's term before denoising, and the depth edges
        //! the denoiser stops at. Internal to these passes.
        const RHI::AttachmentId& DepthChainName()
        {
            static const RHI::AttachmentId s_name("GTAODepth");
            return s_name;
        }

        const RHI::AttachmentId& WorkingTermName()
        {
            static const RHI::AttachmentId s_name("GTAOWorkingTerm");
            return s_name;
        }

        const RHI::AttachmentId& EdgesName()
        {
            static const RHI::AttachmentId s_name("GTAOEdges");
            return s_name;
        }

        //! What the passes take from the view: GTAOCommon.hlsli's fields, given alike to every
        //! Scope of the three, and the main pass's slice count.
        struct FrameInputs
        {
            uint32_t      m_viewIndex     = 0;
            uint32_t      m_temporalNoise = 0;
            Math::Vector2 m_tanHalfFov {0.0f, 0.0f};
            float         m_radius        = 0.0f;
            float         m_intensity     = 0.0f;
            uint32_t      m_sliceCount    = 0;
        };

        //! False on a frame without ambient occlusion: each pass then declares nothing.
        bool TryGetFrameInputs(RHI::RHIContext& ctx, FrameInputs& out)
        {
            const RHI::RHIHandle viewEntity = SceneTextures::AmbientOcclusion::FindView(ctx);
            if (viewEntity == RHI::NullHandle || !TryGetViewIndex(ctx, viewEntity, out.m_viewIndex))
            {
                return false;
            }

            const View& view     = ctx.Get<View>(viewEntity);
            const auto& settings = ctx.Get<ViewAmbientOcclusion>(viewEntity);

            // The depth is unprojected as a perspective one, and over the whole target.
            ASSERT(view.m_viewToClip[2][3] != 0.0f, "[AmbientOcclusionPass] The main view is not a perspective view.");
            ASSERT(view.m_rect.m_minX == 0.0f && view.m_rect.m_maxX == 1.0f
                && view.m_rect.m_minY == 0.0f && view.m_rect.m_maxY == 1.0f,
                "[AmbientOcclusionPass] The main view does not cover its render target.");

            // The projection's diagonal is 1 / tan(half fov), per axis.
            out.m_tanHalfFov = Math::Vector2(1.0f / view.m_viewToClip[0][0], 1.0f / view.m_viewToClip[1][1]);
            // Its noise changes every frame only while something averages the frames.
            out.m_temporalNoise = ctx.Has<ViewTemporalAA>(viewEntity) ? 1u : 0u;
            out.m_radius        = settings.m_radius;
            out.m_intensity     = settings.m_intensity;
            out.m_sliceCount    = settings.m_sliceCount;
            return true;
        }

        void SetFrameInputs(ComputeScope& s, const FrameInputs& in)
        {
            s.Constant(RHI::InputName("viewIndex"), in.m_viewIndex);
            s.Constant(RHI::InputName("temporalNoise"), in.m_temporalNoise);
            s.Constant(RHI::InputName("tanHalfFov"), in.m_tanHalfFov);
            s.Constant(RHI::InputName("effectRadius"), in.m_radius);
            s.Constant(RHI::InputName("intensity"), in.m_intensity);
            // GatherRed and explicit mips only: nothing may be interpolated between depths.
            s.Sampler(RHI::InputName("g_PointSampler"),
                RHI::SamplerState::Create(RHI::FilterMode::Point, RHI::FilterMode::Point, RHI::AddressMode::Clamp));
        }

        Ptr<Resource::ShaderAsset> LoadShader(const char* path)
        {
            auto* assetManager = Service<Resource::AssetManager>::Get();
            ASSERT(assetManager, "AssetManager is unregister.");

            const Resource::AssetId assetId = assetManager->MakeAssetId(path);
            if (!assetId.IsValid())
            {
                LOG_ERROR("[AmbientOcclusionPass] Failed to load shader {}.", path);
                return nullptr;
            }
            return assetManager->LoadAsset<Resource::ShaderAsset>(assetId);
        }

        RHI::ImageDescriptor FullSizeImage(const Math::Vector2Int& size)
        {
            return RHI::ImageDescriptor::Create2D(
                RHI::ImageBindFlags::ShaderReadWrite,
                static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y),
                SceneTextures::AmbientOcclusion::kFormat);
        }
    }

    void AmbientOcclusionPass::SetUp(PassContext& ctx)
    {
        auto prefilterShader = LoadShader("engine://Shaders/AmbientOcclusion/GTAOPrefilterDepths.hlsl");
        auto mainShader      = LoadShader("engine://Shaders/AmbientOcclusion/GTAOMain.hlsl");
        auto denoiseShader   = LoadShader("engine://Shaders/AmbientOcclusion/GTAODenoise.hlsl");
        if (!prefilterShader || !mainShader || !denoiseShader)
        {
            return;
        }

        SPARK_COMPUTE_PASS(ctx, "GTAOPrefilterDepthsPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(prefilterShader)
            .Binds<ViewBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                FrameInputs inputs;
                if (!TryGetFrameInputs(*RHI::RHIExecuteContext::Current(), inputs))
                {
                    return;
                }

                using SceneTextures::AmbientOcclusion::kDepthMipCount;

                const Math::Vector2Int size = p.GetRenderSize();
                RHI::ImageDescriptor desc = FullSizeImage(size);
                desc.m_mipLevels = static_cast<uint16_t>(kDepthMipCount);
                p.CreateImage(DepthChainName(), desc);

                auto s = p.Scope();
                SetFrameInputs(s, inputs);
                s.ReadImage(RHI::AttachmentId("SceneDepth")).Format(RHI::Format::R32_FLOAT)
                    .BindIndex(RHI::InputName("sceneDepthIndex"));

                // All five mips are written by the one dispatch, each through a view of its own.
                static const eastl::array<const char*, kDepthMipCount> s_outputs = {
                    "outDepthIndex0", "outDepthIndex1", "outDepthIndex2", "outDepthIndex3", "outDepthIndex4" };
                for (uint32_t mip = 0; mip < kDepthMipCount; ++mip)
                {
                    const RHI::ImageViewDescriptor view = RHI::ImageViewDescriptor::Create(
                        RHI::Format::Unknown, static_cast<uint16_t>(mip), static_cast<uint16_t>(mip));
                    s.WriteImage(DepthChainName()).View(view).BindIndex(RHI::InputName(s_outputs[mip]));
                }

                // A thread converts 2x2 pixels.
                s.Dispatch(static_cast<uint32_t>(size.x + 1) / 2, static_cast<uint32_t>(size.y + 1) / 2);
                s.Close();
            })
            .Finalize()
        ;

        SPARK_COMPUTE_PASS(ctx, "GTAOMainPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(mainShader)
            .Binds<ViewBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                FrameInputs inputs;
                if (!TryGetFrameInputs(*RHI::RHIExecuteContext::Current(), inputs))
                {
                    return;
                }

                const Math::Vector2Int size = p.GetRenderSize();
                p.CreateImage(WorkingTermName(), FullSizeImage(size));
                p.CreateImage(EdgesName(), FullSizeImage(size));

                auto s = p.Scope();
                SetFrameInputs(s, inputs);
                s.ReadImage(DepthChainName()).BindIndex(RHI::InputName("depthIndex"));
                s.ReadImage(RHI::AttachmentId("GBufferNormal")).BindIndex(RHI::InputName("normalIndex"));
                s.WriteImage(WorkingTermName()).BindIndex(RHI::InputName("outTermIndex"));
                s.WriteImage(EdgesName()).BindIndex(RHI::InputName("outEdgesIndex"));
                s.Constant(RHI::InputName("sliceCount"), inputs.m_sliceCount);
                s.Dispatch(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));
                s.Close();
            })
            .Finalize()
        ;

        SPARK_COMPUTE_PASS(ctx, "GTAODenoisePass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(denoiseShader)
            .Binds<ViewBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                FrameInputs inputs;
                if (!TryGetFrameInputs(*RHI::RHIExecuteContext::Current(), inputs))
                {
                    return;
                }

                const Math::Vector2Int size = p.GetRenderSize();
                p.CreateImage(SceneTextures::AmbientOcclusion::Name(), FullSizeImage(size));

                auto s = p.Scope();
                SetFrameInputs(s, inputs);
                s.ReadImage(WorkingTermName()).BindIndex(RHI::InputName("termIndex"));
                s.ReadImage(EdgesName()).BindIndex(RHI::InputName("edgesIndex"));
                s.WriteImage(SceneTextures::AmbientOcclusion::Name()).BindIndex(RHI::InputName("outputIndex"));

                // A thread denoises two horizontal pixels.
                s.Dispatch(static_cast<uint32_t>(size.x + 1) / 2, static_cast<uint32_t>(size.y));
                s.Close();
            })
            .Finalize()
        ;
    }
}
