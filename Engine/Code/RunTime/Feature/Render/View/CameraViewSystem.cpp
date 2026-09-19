#include "CameraViewSystem.h"

#include <EASTL/fixed_vector.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <Feature/Camera/Components.h>
#include <Feature/AntiAliasing/Components.h>

#include "View.h"
#include "ViewComponents.h"
#include "ViewTags.h"
#include "ViewFactory.h"

namespace Spark::Render
{
    namespace
    {
        //! Halton(2,3) in [-0.5, 0.5) pixels, converted to NDC. Starts at index 1: index 0 is
        //! the pixel centre in both bases. Pixel y runs down and NDC y up, hence the sign.
        Math::Vector2 TemporalJitter(uint64_t frameNumber, uint32_t sampleCount, const Math::Vector2Int& size)
        {
            const uint32_t index = static_cast<uint32_t>(frameNumber % sampleCount) + 1;
            const float    x     = Math::Halton(index, 2) - 0.5f;
            const float    y     = Math::Halton(index, 3) - 0.5f;
            return Math::Vector2(
                 2.0f * x / static_cast<float>(size.x),
                -2.0f * y / static_cast<float>(size.y));
        }

        ViewTemporalAA ValidateTemporalAA(const AntiAliasing::TemporalAAComponent& c)
        {
            ViewTemporalAA v;
            v.m_currentFrameWeight = Math::Clamp(c.m_currentFrameWeight, 0.02f, 0.5f);
            v.m_motionFrameWeight  = Math::Clamp(c.m_motionFrameWeight, v.m_currentFrameWeight, 1.0f);
            v.m_varianceClipGamma  = Math::Clamp(c.m_varianceClipGamma, 0.75f, 2.0f);
            v.m_filterSize         = Math::Clamp(c.m_filterSize, 0.5f, 2.0f);
            switch (c.m_jitterSamples)
            {
            case AntiAliasing::TemporalAAJitterSamples::Four:
            case AntiAliasing::TemporalAAJitterSamples::Eight:
            case AntiAliasing::TemporalAAJitterSamples::Sixteen:
                v.m_jitterSamples = static_cast<uint32_t>(c.m_jitterSamples);
                break;
            default:
                v.m_jitterSamples = 8;
                break;
            }
            return v;
        }

        template<typename Ref>
        void ReapOrphans(WorldContext& world, RHI::RHIContext& rhiCtx)
        {
            eastl::fixed_vector<Entity, 4> orphans;
            world.GetView<Ref>().each([&](Entity e, const Ref& ref)
            {
                if (world.Has<DeadTag>(e) || !world.Has<Camera::CameraComponent>(e))
                {
                    DestroyViewEntity(rhiCtx, ref.m_view);
                    orphans.push_back(e);
                }
            });
            for (Entity e : orphans)
            {
                world.Remove<Ref>(e);
            }
        }

        template<typename Tag, typename Ref>
        Ref* FindOrCreateView(WorldContext& world, RHI::RHIContext& rhiCtx, Entity source)
        {
            if (auto* ref = world.TryGet<Ref>(source))
            {
                return ref;
            }
            const RHI::RHIHandle created = CreateViewEntity<Tag>(rhiCtx);
            if (created == RHI::NullHandle)
            {
                return nullptr;
            }
            return &world.Add<Ref>(source, Ref{ created });
        }
    }

    void CameraViewSystem::Update(const Math::Vector2Int& renderSize,
                                  const Math::Vector2Int& outputOrigin, const Math::Vector2Int& outputSize,
                                  const Math::Vector2Int& outputBufferSize,
                                  const FrameTime& time)
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx || renderSize.x <= 0 || renderSize.y <= 0 || outputSize.y <= 0
            || outputBufferSize.x <= 0 || outputBufferSize.y <= 0)
        {
            return;   // a minimized / zero framebuffer would divide by zero below
        }

        ReapOrphans<MainViewRef>(*world, *rhiCtx);
        ReapOrphans<OutputViewRef>(*world, *rhiCtx);

        // Projection is built HERE, not in CameraSystem: aspect is a render-target
        // property the world layer doesn't know.
        const float aspect = static_cast<float>(outputSize.x) / static_cast<float>(outputSize.y);

        world->GetView<Camera::CameraComponent, Camera::CameraViewMatrix>(Exclude<DeadTag>).each(
            [&](Entity e, const Camera::CameraComponent& camera, const Camera::CameraViewMatrix& mats)
        {
            const bool hadMainView = world->Has<MainViewRef>(e);
            auto* mainRef   = FindOrCreateView<MainViewTag, MainViewRef>(*world, *rhiCtx, e);
            auto* outputRef = FindOrCreateView<OutputViewTag, OutputViewRef>(*world, *rhiCtx, e);
            if (!mainRef || !outputRef)
            {
                return;
            }
            if (!hadMainView)
            {
                rhiCtx->Add<ViewHistory>(mainRef->m_view);
            }

            // Both views exist before either View is referenced: creating one grows the pool.
            View& view = rhiCtx->Get<View>(mainRef->m_view);
            view.m_worldToView = mats.m_viewMatrix;
            view.m_viewToClip  = Math::PerspectiveFov(Math::Radians(camera.m_fov), aspect, camera.m_clipStart, camera.m_clipEnd);
            view.m_bufferSize  = renderSize;
            view.m_jitter      = Math::Vector2(0.0f, 0.0f);

            // Jitter only pays off with something accumulating it.
            if (const auto* taa = world->TryGet<AntiAliasing::TemporalAAComponent>(e))
            {
                const ViewTemporalAA validated = ValidateTemporalAA(*taa);
                view.m_jitter = TemporalJitter(time.m_frameNumber, validated.m_jitterSamples, renderSize);
                rhiCtx->AddOrReplace<ViewTemporalAA>(mainRef->m_view, validated);
            }
            else if (rhiCtx->Has<ViewTemporalAA>(mainRef->m_view))
            {
                rhiCtx->Remove<ViewTemporalAA>(mainRef->m_view);
            }

            rhiCtx->AddOrReplace<ViewFrustum>(mainRef->m_view, ViewFrustum{ Math::Frustum::FromViewProjection(view.GetWorldToClip()) });

            // Same camera, placed in the displayed target instead of the render targets.
            View& output = rhiCtx->Get<View>(outputRef->m_view);
            output = view;
            output.m_jitter          = Math::Vector2(0.0f, 0.0f);
            output.m_inputRect       = view.m_rect;
            output.m_inputBufferSize = view.m_bufferSize;
            output.m_bufferSize      = outputBufferSize;
            output.m_rect.m_minX = static_cast<float>(outputOrigin.x) / outputBufferSize.x;
            output.m_rect.m_maxX = static_cast<float>(outputOrigin.x + outputSize.x) / outputBufferSize.x;
            output.m_rect.m_minY = static_cast<float>(outputOrigin.y) / outputBufferSize.y;
            output.m_rect.m_maxY = static_cast<float>(outputOrigin.y + outputSize.y) / outputBufferSize.y;
        });
    }

    void CameraViewSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        rhiCtx.GetView<MainViewTag, ViewShaderBindings>().each(
            [&](RHI::RHIHandle view, const ViewShaderBindings&)
        {
            DestroyViewEntity(rhiCtx, view);
        });
        rhiCtx.GetView<OutputViewTag, ViewShaderBindings>().each(
            [&](RHI::RHIHandle view, const ViewShaderBindings&)
        {
            DestroyViewEntity(rhiCtx, view);
        });

        // Strip the world-side refs so a re-init starts clean.
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<MainViewRef>();
            world->Clear<OutputViewRef>();
        }
    }
}
