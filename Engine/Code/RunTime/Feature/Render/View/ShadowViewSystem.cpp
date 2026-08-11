#include "ShadowViewSystem.h"

#include <EASTL/fixed_vector.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Math/MathUtils.h>

#include <RHI/HardwareQueue.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Image/ImageDescriptor.h>

#include <Light/Components.h>

#include "View.h"
#include "ViewComponents.h"
#include "ViewFactory.h"
#include "ViewTags.h"

namespace Spark::Render
{
    namespace
    {
        //! PLACEHOLDER, both of them. The ortho box should be fitted to the camera frustum
        //! (a bounding sphere of the shadow-distance slice, so its size is invariant under
        //! camera rotation) and the pull-back derived from the scene bounds so that casters
        //! between the light and the viewer are not clipped away. Neither the authored shadow
        //! distance nor a scene AABB exists yet — see TODO_MultiViewPlan.md §五.
        constexpr float kDirectionalHalfExtent = 30.0f;
        constexpr float kDirectionalPullback   = 200.0f;

        constexpr float kSpotNearZ = 0.05f;

        //! tan(fov/2) is degenerate at both endpoints of the authored cone. A numeric guard,
        //! not a quality cap.
        constexpr float kSpotMinHalfAngleDeg = 1.0f;
        constexpr float kSpotMaxHalfAngleDeg = 89.0f;

        //! An up vector that is not parallel to dir, so LookAt stays well conditioned.
        Math::Vector3 StableUp(const Math::Vector3& dir)
        {
            return (Math::Abs(dir.y) > 0.99f)
                ? Math::Vector3(0.0f, 0.0f, 1.0f)
                : Math::Vector3(0.0f, 1.0f, 0.0f);
        }

        //! Point lights need six views and a cube face mapping, which is not wired yet.
        bool ProducesShadowView(const Light::LightRenderData& rd)
        {
            return rd.m_castShadow
                && (rd.m_type == Light::LightType::Directional
                 || rd.m_type == Light::LightType::Spot);
        }

        //! Where the main camera is. The view entities already answer "which camera is the
        //! main one" — split screen, editor viewports and all — so this system reads that
        //! answer instead of forming its own. Origin during warmup, when no main view exists.
        Math::Vector3 MainViewPosition(RHI::RHIContext& rhiCtx)
        {
            Math::Vector3 position(0.0f, 0.0f, 0.0f);
            bool found = false;
            rhiCtx.GetView<MainViewTag, View>(Exclude<DeadTag>).each([&](RHI::RHIHandle, const View& view)
            {
                if (!found)
                {
                    position = Math::Vector3(Math::Inverse(view.m_worldToView)[3]);
                    found    = true;
                }
            });
            return position;
        }

        //! A directional light has no position, so the box's placement is chosen rather than
        //! read: it follows the camera, since that is the only region whose shadows are seen.
        //! The eye is pulled back along the light direction so that casters standing between
        //! the light and that region still fall inside the near plane.
        void WriteDirectionalView(View& view, const Light::LightRenderData& rd, const Math::Vector3& focus)
        {
            const Math::Vector3 dir = Math::Normalize(rd.m_worldDirection);
            view.m_worldToView = Math::LookAt(focus - dir * kDirectionalPullback, focus, StableUp(dir));
            view.m_viewToClip  = Math::OrthographicProjection(
                -kDirectionalHalfExtent, kDirectionalHalfExtent,
                -kDirectionalHalfExtent, kDirectionalHalfExtent,
                0.0f, kDirectionalPullback + kDirectionalHalfExtent);
        }

        void WriteSpotView(View& view, const Light::LightRenderData& rd)
        {
            const Math::Vector3 dir = Math::Normalize(rd.m_worldDirection);
            view.m_worldToView = Math::LookAt(rd.m_worldPosition, rd.m_worldPosition + dir, StableUp(dir));

            // m_cosOuter is the cosine of the HALF angle, so the full vertical fov is twice
            // its arccos. Square tile, hence aspect 1.
            const float outerHalf = Math::Clamp(
                Math::Acos(Math::Clamp(rd.m_cosOuter, -1.0f, 1.0f)),
                Math::Radians(kSpotMinHalfAngleDeg), Math::Radians(kSpotMaxHalfAngleDeg));
            const float farZ      = rd.m_range > kSpotNearZ ? rd.m_range : kSpotNearZ * 2.0f;
            view.m_viewToClip     = Math::PerspectiveFov(outerHalf * 2.0f, 1.0f, kSpotNearZ, farZ);
        }
    }

    void ShadowViewSystem::Init(RHI::RHIContext& rhiCtx)
    {
        auto desc = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::DepthStencil | RHI::ImageBindFlags::ShaderRead,
            kShadowAtlasResolution, kShadowAtlasResolution, kShadowAtlasFormat);
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        m_atlas = RHI::CreateImportedImage(rhiCtx, ObjectName("ShadowAtlas"), desc);
        rhiCtx.Add<ShadowAtlasTag>(m_atlas);
    }

    void ShadowViewSystem::Update()
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx)
        {
            return;
        }

        // Sweep: a light that died, lost its render data or stopped casting hands its views
        // and its tiles back.
        eastl::fixed_vector<Entity, 8> orphans;
        world->GetView<ShadowViewRefs>().each([&](Entity e, const ShadowViewRefs& refs)
        {
            const auto* rd = world->TryGet<Light::LightRenderData>(e);
            if (!world->Has<DeadTag>(e) && rd && ProducesShadowView(*rd))
            {
                return;
            }

            for (RHI::RHIHandle v : refs.m_views)
            {
                if (const auto* slot = rhiCtx->TryGet<ShadowViewSlot>(v))
                {
                    ReleaseSlot(slot->m_slot);
                }
                DestroyViewEntity(*rhiCtx, v);
            }
            LOG_INFO("[ShadowViewSystem] Light {} released {} shadow view(s).",
                static_cast<uint32_t>(e), refs.m_views.size());
            orphans.push_back(e);
        });
        for (Entity e : orphans)
        {
            world->Remove<ShadowViewRefs>(e);
        }

        const Math::Vector3 focus = MainViewPosition(*rhiCtx);

        world->GetView<Light::LightRenderData>(Exclude<DeadTag>).each([&](Entity e, const Light::LightRenderData& rd)
        {
            if (!ProducesShadowView(rd))
            {
                return;
            }

            auto* refs = world->TryGet<ShadowViewRefs>(e);
            if (!refs)
            {
                const uint32_t slot = AllocateSlot();
                if (slot == kInvalidShadowSlot)
                {
                    // Atlas full: this light casts no shadow. No ShadowViewRefs, so m_index
                    // stays absent and the lighting shader samples no tile — degraded, not
                    // wrong. Retried next frame, in case a tile frees up.
                    return;
                }

                const RHI::RHIHandle created = CreateViewEntity<ShadowViewTag>(*rhiCtx);
                if (created == RHI::NullHandle)
                {
                    ReleaseSlot(slot);
                    return;
                }
                rhiCtx->Add<ShadowViewSlot>(created, ShadowViewSlot{ slot });

                ShadowViewRefs added;
                added.m_views.push_back(created);
                added.m_index = static_cast<int32_t>(slot);
                refs = &world->Add<ShadowViewRefs>(e, eastl::move(added));

                LOG_INFO("[ShadowViewSystem] Light {} took shadow tile {}.",
                    static_cast<uint32_t>(e), slot);
            }

            ASSERT(refs->m_index >= 0, "[ShadowViewSystem] Light {} has shadow views but no tile.", static_cast<uint32_t>(e));

            View& view  = rhiCtx->Get<View>(refs->m_views[0]);
            view.m_rect = ShadowTileRect(static_cast<uint32_t>(refs->m_index));
            if (rd.m_type == Light::LightType::Directional)
            {
                WriteDirectionalView(view, rd, focus);
            }
            else
            {
                WriteSpotView(view, rd);
            }
        });
    }

    void ShadowViewSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        rhiCtx.GetView<ShadowViewTag, ViewShaderBindings>().each(
            [&](RHI::RHIHandle view, const ViewShaderBindings&)
        {
            DestroyViewEntity(rhiCtx, view);
        });
        if (m_atlas != RHI::NullHandle && rhiCtx.Valid(m_atlas))
        {
            rhiCtx.DestoryEntity(m_atlas);
        }
        m_atlas = RHI::NullHandle;

        m_slots.reset();
        m_atlasFullLogged = false;

        // Strip the world-side refs so a re-init starts clean.
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<ShadowViewRefs>();
        }
    }

    uint32_t ShadowViewSystem::AllocateSlot()
    {
        for (uint32_t i = 0; i < kShadowTileCount; ++i)
        {
            if (!m_slots.test(i))
            {
                m_slots.set(i);
                return i;
            }
        }

        if (!m_atlasFullLogged)
        {
            LOG_WARN("[ShadowViewSystem] All {} shadow tiles are taken; further lights cast "
                     "no shadow until one frees up.", kShadowTileCount);
            m_atlasFullLogged = true;
        }
        return kInvalidShadowSlot;
    }

    void ShadowViewSystem::ReleaseSlot(uint32_t slot)
    {
        if (slot < kShadowTileCount)
        {
            m_slots.set(slot, false);
            m_atlasFullLogged = false;
        }
    }
}
