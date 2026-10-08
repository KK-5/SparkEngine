#include "ScreenSpaceReflectionsPass.h"

#include <EASTL/array.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassContext.h>
#include <Pass/ComputePass.h>

#include <RenderGraph/PassScopes.h>

#include <Binding/View/ViewBinding.h>

#include <View/View.h>
#include <View/ViewComponents.h>

#include <Feature/PostProcess/PostProcessResources.h>
#include <Feature/SceneTextures/SceneTextures.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    void ScreenSpaceReflectionsPass::SetUp(PassContext& ctx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        const Resource::AssetId assetId =
            assetManager->MakeAssetId("engine://Shaders/ScreenSpaceReflections/ScreenSpaceReflections.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[ScreenSpaceReflectionsPass] Failed to load shader ScreenSpaceReflections.hlsl.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        SPARK_COMPUTE_PASS(ctx, "ScreenSpaceReflectionsPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Binds<ViewBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                using namespace SceneTextures;

                auto& rhiContext = *RHI::RHIExecuteContext::Current();

                // Declaring nothing skips the pass this frame.
                const RHI::RHIHandle viewEntity = ScreenSpaceReflections::FindView(rhiContext);
                uint32_t             viewIndex  = 0;
                if (viewEntity == RHI::NullHandle || !TryGetViewIndex(rhiContext, viewEntity, viewIndex))
                {
                    return;
                }

                const View& view     = rhiContext.Get<View>(viewEntity);
                const auto& settings = rhiContext.Get<ViewScreenSpaceReflection>(viewEntity);

                // A pixel's screen position is taken over the whole target.
                ASSERT(view.m_rect.m_minX == 0.0f && view.m_rect.m_maxX == 1.0f
                    && view.m_rect.m_minY == 0.0f && view.m_rect.m_maxY == 1.0f,
                    "[ScreenSpaceReflectionsPass] The main view does not cover its render target.");

                const Math::Vector2Int size    = p.GetRenderSize();
                const Math::Vector2Int hzbSize = HZB::MipSize(size, 0);

                p.CreateImage(ScreenSpaceReflections::Name(), RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::ShaderReadWrite,
                    static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y),
                    ScreenSpaceReflections::kFormat));

                auto s = p.Scope();
                s.Constant(RHI::InputName("viewIndex"), viewIndex);
                s.ReadImage(RHI::AttachmentId("SceneDepth")).Format(RHI::Format::R32_FLOAT)
                    .Bind(RHI::InputName("g_Depth"));
                s.ReadImage(HZB::ClosestName()).Bind(RHI::InputName("g_HZBClosest"));
                s.ReadImage(RHI::AttachmentId("GBufferNormal")).Bind(RHI::InputName("g_GBufferNormal"));
                s.ReadImage(RHI::AttachmentId("GBufferSurface")).Bind(RHI::InputName("g_GBufferSurface"));
                s.ReadImage(RHI::AttachmentId("ResolvedVelocity")).Bind(RHI::InputName("g_Velocity"));
                // What a hit looked like: the finished image of last frame, which TemporalAAPass
                // writes later in this one.
                s.ReadPreviousImage(PostProcess::TemporalAAName())
                    .Bind(RHI::InputName("g_History"))
                    .BindValid(RHI::InputName("g_HistoryValid"));
                // What stands in for it on a frame without one: the scene as lit so far, before
                // ReflectionsPass and the sky are added to it.
                s.ReadImage(RHI::AttachmentId("SceneColor")).Bind(RHI::InputName("g_SceneColor"));
                s.WriteImage(ScreenSpaceReflections::Name()).Bind(RHI::InputName("g_Output"));

                s.Constant(RHI::InputName("g_HZBUvFactor"), HZB::UvFactor(size));
                s.Constant(RHI::InputName("g_HZBSize"), eastl::array<uint32_t, 2>{
                    static_cast<uint32_t>(hzbSize.x), static_cast<uint32_t>(hzbSize.y) });
                s.Constant(RHI::InputName("g_HZBMipCount"), HZB::MipCount(size));
                s.Constant(RHI::InputName("g_StepCountMax"), settings.m_maxSteps);
                s.Constant(RHI::InputName("g_MaxRoughness"), settings.m_maxRoughness);
                s.Constant(RHI::InputName("g_Intensity"), settings.m_intensity);
                s.Sampler(RHI::InputName("g_LinearSampler"),
                    RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp));

                s.Dispatch(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));
                s.Close();
            })
            .Finalize()
        ;
    }
}
