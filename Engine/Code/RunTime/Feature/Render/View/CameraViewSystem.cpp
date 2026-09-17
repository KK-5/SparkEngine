#include "CameraViewSystem.h"

#include <EASTL/fixed_vector.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <Feature/Camera/Components.h>

#include "View.h"
#include "ViewComponents.h"
#include "ViewTags.h"
#include "ViewFactory.h"

namespace Spark::Render
{
    namespace
    {
        constexpr uint32_t kJitterSampleCount = 8;

        //! Halton(2,3) in [-0.5, 0.5) pixels, converted to NDC. Starts at index 1: index 0 is
        //! the pixel centre in both bases. Pixel y runs down and NDC y up, hence the sign.
        Math::Vector2 TemporalJitter(uint64_t frameNumber, const Math::Vector2Int& size)
        {
            const uint32_t index = static_cast<uint32_t>(frameNumber % kJitterSampleCount) + 1;
            const float    x     = Math::Halton(index, 2) - 0.5f;
            const float    y     = Math::Halton(index, 3) - 0.5f;
            return Math::Vector2(
                 2.0f * x / static_cast<float>(size.x),
                -2.0f * y / static_cast<float>(size.y));
        }
    }

    void CameraViewSystem::Update(const Math::Vector2Int& renderSize, const FrameTime& time, bool jitterEnabled)
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx || renderSize.y <= 0)
        {
            return;   // a minimized / zero framebuffer would divide by zero below
        }

        eastl::fixed_vector<Entity, 4> orphans;
        world->GetView<MainViewRef>().each([&](Entity e, const MainViewRef& ref)
        {
            if (world->Has<DeadTag>(e) || !world->Has<Camera::CameraComponent>(e))
            {
                DestroyViewEntity(*rhiCtx, ref.m_view);
                orphans.push_back(e);
            }
        });
        for (Entity e : orphans)
        {
            world->Remove<MainViewRef>(e);
        }

        // Projection is built HERE, not in CameraSystem: aspect is a render-target
        // property the world layer doesn't know.
        const float aspect = static_cast<float>(renderSize.x) / static_cast<float>(renderSize.y);

        world->GetView<Camera::CameraComponent, Camera::CameraViewMatrix>(Exclude<DeadTag>).each(
            [&](Entity e, const Camera::CameraComponent& camera, const Camera::CameraViewMatrix& mats)
        {
            auto* ref = world->TryGet<MainViewRef>(e);
            if (!ref)
            {
                const RHI::RHIHandle created = CreateViewEntity<MainViewTag>(*rhiCtx);
                if (created == RHI::NullHandle)
                {
                    return;
                }
                rhiCtx->Add<ViewHistory>(created);
                ref = &world->Add<MainViewRef>(e, MainViewRef{ created });
            }

            View& view = rhiCtx->Get<View>(ref->m_view);
            view.m_worldToView = mats.m_viewMatrix;
            view.m_viewToClip  = Math::PerspectiveFov(Math::Radians(camera.m_fov), aspect, camera.m_clipStart, camera.m_clipEnd);
            view.m_bufferSize  = renderSize;
            view.m_jitter      = jitterEnabled ? TemporalJitter(time.m_frameNumber, renderSize) : Math::Vector2(0.0f, 0.0f);

            rhiCtx->AddOrReplace<ViewFrustum>(ref->m_view, ViewFrustum{ Math::Frustum::FromViewProjection(view.GetWorldToClip()) });
        });
    }

    void CameraViewSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        rhiCtx.GetView<MainViewTag, ViewShaderBindings>().each(
            [&](RHI::RHIHandle view, const ViewShaderBindings&)
        {
            DestroyViewEntity(rhiCtx, view);
        });

        // Strip the world-side refs so a re-init starts clean.
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<MainViewRef>();
        }
    }
}
