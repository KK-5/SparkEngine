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
    void CameraViewSystem::Update(const Math::Vector2Int& renderSize)
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
                ref = &world->Add<MainViewRef>(e, MainViewRef{ created });
            }

            View& view = rhiCtx->Get<View>(ref->m_view);
            view.m_worldToView = mats.m_viewMatrix;
            view.m_viewToClip  = Math::PerspectiveFov(Math::Radians(camera.m_fov), aspect, camera.m_clipStart, camera.m_clipEnd);

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
