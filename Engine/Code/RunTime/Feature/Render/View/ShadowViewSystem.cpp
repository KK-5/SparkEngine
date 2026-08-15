#include "ShadowViewSystem.h"

#include <EASTL/fixed_vector.h>
#include <EASTL/sort.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Math/Frustum.h>
#include <Math/MathUtils.h>
#include <Math/Sphere.h>

#include <RHI/HardwareQueue.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Image/ImageDescriptor.h>

#include <Light/Components.h>

#include "RenderGraph/RenderGraphUtils.h"   // IsResourceReady

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
        constexpr float kDirectionalHalfExtent = 12.0f;
        constexpr float kDirectionalPullback   = 200.0f;

        //! World size of one shadow texel for that box. The divisor is the tile's USABLE
        //! span, not its resolution: the viewport is inset by the border, so that is what
        //! NDC [-1,1] lands on.
        constexpr float kDirectionalTexelSize =
            2.0f * kDirectionalHalfExtent / static_cast<float>(kShadowTileUsableTexels);

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

        //! A light must clear kScoreEnter to take a tile but only kScoreExit to keep one, so
        //! one hovering at the boundary does not flicker on and off.
        constexpr float kScoreEnter = 0.03f;
        constexpr float kScoreExit  = 0.02f;

        //! Ranking bonus for a light that already holds a tile: a newcomer has to be clearly
        //! more important to take it over, not a hair ahead.
        constexpr float kIncumbentBonus = 1.25f;

        //! Sentinel for lights whose screen radius cannot be derived from geometry: a
        //! directional light has no bounding volume, and a camera standing inside a sphere
        //! degenerates the tangent cone. Ranked like any other score, not exempt from the
        //! budget.
        constexpr float kScoreMax = 1e9f;

        struct MainViewInfo
        {
            Math::Vector3 m_eye {0.0f, 0.0f, 0.0f};
            Math::Frustum m_frustum {};
            float         m_proj11 = 1.0f;   // 1 / tan(fovY / 2)

            //! False during warmup and with no camera, and then nothing is culled: letting
            //! through what should be rejected costs a tile, rejecting what should pass
            //! loses a shadow.
            bool          m_valid = false;
        };

        MainViewInfo ResolveMainView(RHI::RHIContext& rhiCtx)
        {
            MainViewInfo info;
            bool found = false;

            rhiCtx.GetView<MainViewTag, View>(Exclude<DeadTag>).each(
                [&](RHI::RHIHandle e, const View& view)
            {
                if (found)
                {
                    return;
                }
                found = true;

                info.m_eye    = Math::Vector3(Math::Inverse(view.m_worldToView)[3]);
                info.m_proj11 = view.m_viewToClip[1][1];
                if (const auto* frustum = rhiCtx.TryGet<ViewFrustum>(e))
                {
                    info.m_frustum = frustum->m_frustum;
                    info.m_valid   = true;
                }
            });
            return info;
        }

        //! Radius of the light's bounding sphere projected into NDC. In NDC rather than
        //! pixels because this runs before the render graph, with no attachment extent to
        //! scale by.
        float ScreenRadius(const Math::Sphere& sphere, const MainViewInfo& main)
        {
            const Math::Vector3 toCenter = sphere.center - main.m_eye;
            const float d2 = Math::Dot(toCenter, toCenter);
            const float r2 = sphere.radius * sphere.radius;
            if (d2 <= r2)
            {
                return kScoreMax;
            }
            return sphere.radius * main.m_proj11 / Math::Sqrt(d2 - r2);
        }

        struct Candidate
        {
            Entity m_light = NullEntity;
            float  m_score = 0.0f;
            bool   m_held  = false;
        };

        float RankOf(const Candidate& c)
        {
            return c.m_score * (c.m_held ? kIncumbentBonus : 1.0f);
        }

        //! Quantize the box origin to whole texels in the light's own frame. Without it the
        //! texel grid slides continuously under static geometry as the camera moves and
        //! shadow edges crawl. Only x/y are snapped — z runs along the light, where the grid
        //! does not live.
        //!
        //! This works only because the box size is FIXED. A frustum-fitted box changes size
        //! with camera rotation, leaving no constant grid to quantize against; that is why
        //! fitting has to arrive together with its own stabilization (a bounding sphere),
        //! not on top of this.
        Math::Vector3 SnapToTexelGrid(
            const Math::Vector3& focus, const Math::Vector3& dir, const Math::Vector3& up)
        {
            const Math::Matrix4X4 basis = Math::LookAt(Math::Vector3(0.0f, 0.0f, 0.0f), dir, up);

            Math::Vector3 lightSpace = Math::Vector3(basis * Math::Vector4(focus, 1.0f));
            lightSpace.x = Math::Floor(lightSpace.x / kDirectionalTexelSize) * kDirectionalTexelSize;
            lightSpace.y = Math::Floor(lightSpace.y / kDirectionalTexelSize) * kDirectionalTexelSize;

            return Math::Vector3(Math::Inverse(basis) * Math::Vector4(lightSpace, 1.0f));
        }

        //! A directional light has no position, so the box's placement is chosen rather than
        //! read: it follows the camera, since that is the only region whose shadows are seen.
        //! The eye is pulled back along the light direction so that casters standing between
        //! the light and that region still fall inside the near plane.
        void WriteDirectionalView(View& view, const Light::LightRenderData& rd, const Math::Vector3& cameraPos)
        {
            const Math::Vector3 dir   = Math::Normalize(rd.m_worldDirection);
            const Math::Vector3 up    = StableUp(dir);
            const Math::Vector3 focus = SnapToTexelGrid(cameraPos, dir, up);

            view.m_worldToView = Math::LookAt(focus - dir * kDirectionalPullback, focus, up);
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

        // The single readiness gate for shadows. Holding tiles back until the atlas exists
        // leaves every m_shadowIndex at -1, which is what the lighting shader already tests,
        // so nothing downstream needs a second check for the warmup frames.
        if (!IsResourceReady(*rhiCtx, m_atlas))
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
                if (const auto* tile = rhiCtx->TryGet<ShadowAtlasTile>(v))
                {
                    ReleaseTile(tile->m_tile);
                }
                if (const auto* row = rhiCtx->TryGet<ShadowViewIndex>(v))
                {
                    ReleaseViewIndex(row->m_index);
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

        const MainViewInfo main = ResolveMainView(*rhiCtx);

        eastl::fixed_vector<Candidate, 32> candidates;
        world->GetView<Light::LightRenderData>(Exclude<DeadTag>).each(
            [&](Entity e, const Light::LightRenderData& rd)
        {
            if (!ProducesShadowView(rd))
            {
                return;
            }

            const auto* refs = world->TryGet<ShadowViewRefs>(e);
            const bool  held = refs && refs->m_baseIndex >= 0;

            // No LightBounds means unbounded influence, so nothing here can reject it.
            float score = kScoreMax;
            if (const auto* bounds = world->TryGet<Light::LightBounds>(e); bounds && main.m_valid)
            {
                if (!main.m_frustum.IntersectsSphere(bounds->m_sphere.center, bounds->m_sphere.radius))
                {
                    Deactivate(*world, *rhiCtx, e);
                    return;
                }
                score = ScreenRadius(bounds->m_sphere, main);
                if (score < (held ? kScoreExit : kScoreEnter))
                {
                    Deactivate(*world, *rhiCtx, e);
                    return;
                }
            }
            candidates.push_back(Candidate{ e, score, held });
        });

        eastl::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return RankOf(a) > RankOf(b); });

        // Losers first: a tile handed back here is available to a winner below in the same
        // frame.
        for (size_t i = kShadowTileCount; i < candidates.size(); ++i)
        {
            Deactivate(*world, *rhiCtx, candidates[i].m_light);
        }
        const size_t winners = candidates.size() < kShadowTileCount ? candidates.size() : kShadowTileCount;
        for (size_t i = 0; i < winners; ++i)
        {
            Activate(*world, *rhiCtx, candidates[i].m_light, main.m_eye);
        }
    }

    void ShadowViewSystem::Activate(
        WorldContext& world, RHI::RHIContext& rhiCtx, Entity light, const Math::Vector3& focus)
    {
        const auto* rd = world.TryGet<Light::LightRenderData>(light);
        if (!rd)
        {
            return;
        }

        auto* refs = world.TryGet<ShadowViewRefs>(light);
        if (!refs)
        {
            const RHI::RHIHandle created = CreateViewEntity<ShadowViewTag>(rhiCtx);
            if (created == RHI::NullHandle)
            {
                return;
            }
            ShadowViewRefs added;
            added.m_views.push_back(created);
            refs = &world.Add<ShadowViewRefs>(light, eastl::move(added));
        }

        // One view per light today. Point lights add five more faces and turn this into a
        // loop — see TODO_ShadowOptimizePlan.md §七 step 3.
        const RHI::RHIHandle viewHandle = refs->m_views[0];

        if (refs->m_baseIndex < 0)
        {
            const uint32_t tile = AllocateTile();
            if (tile == kInvalidShadowSlot)
            {
                return;
            }
            const uint32_t row = AllocateViewIndex();
            if (row == kInvalidShadowSlot)
            {
                ReleaseTile(tile);
                return;
            }
            refs->m_baseIndex = static_cast<int32_t>(row);

            rhiCtx.AddOrReplace<ShadowAtlasTile>(viewHandle, ShadowAtlasTile{ tile });
            rhiCtx.AddOrReplace<ShadowViewIndex>(viewHandle, ShadowViewIndex{ row });
            rhiCtx.Remove<ViewInactiveTag>(viewHandle);

            LOG_INFO("[ShadowViewSystem] Light {} took shadow tile {} (view row {}).",
                static_cast<uint32_t>(light), tile, row);
        }

        View& view  = rhiCtx.Get<View>(viewHandle);
        view.m_rect = ShadowTileRect(rhiCtx.Get<ShadowAtlasTile>(viewHandle).m_tile);
        if (rd->m_type == Light::LightType::Directional)
        {
            WriteDirectionalView(view, *rd, focus);
        }
        else
        {
            WriteSpotView(view, *rd);
        }
    }

    void ShadowViewSystem::Deactivate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light)
    {
        auto* refs = world.TryGet<ShadowViewRefs>(light);
        if (!refs || refs->m_baseIndex < 0)
        {
            return;
        }

        for (RHI::RHIHandle v : refs->m_views)
        {
            if (const auto* tile = rhiCtx.TryGet<ShadowAtlasTile>(v))
            {
                ReleaseTile(tile->m_tile);
            }
            if (const auto* row = rhiCtx.TryGet<ShadowViewIndex>(v))
            {
                ReleaseViewIndex(row->m_index);
            }
            rhiCtx.Remove<ShadowAtlasTile>(v);
            rhiCtx.Remove<ShadowViewIndex>(v);
            if (!rhiCtx.Has<ViewInactiveTag>(v))
            {
                rhiCtx.Add<ViewInactiveTag>(v);
            }
        }
        refs->m_baseIndex = -1;

        LOG_INFO("[ShadowViewSystem] Light {} gave its shadow tile back.",
            static_cast<uint32_t>(light));
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

        m_blocks.Reset();
        m_viewRows.reset();
        m_atlasFullLogged = false;

        // Strip the world-side refs so a re-init starts clean.
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<ShadowViewRefs>();
        }
    }

    uint32_t ShadowViewSystem::AllocateTile()
    {
        const uint32_t node = m_blocks.Allocate(kShadowTileLevel);
        if (node != ShadowAtlasAllocator::kInvalidNode)
        {
            return node;
        }

        if (!m_atlasFullLogged)
        {
            LOG_WARN("[ShadowViewSystem] All {} shadow tiles are taken; further lights cast "
                     "no shadow until one frees up.", kShadowTileCount);
            m_atlasFullLogged = true;
        }
        return kInvalidShadowSlot;
    }

    void ShadowViewSystem::ReleaseTile(uint32_t tile)
    {
        m_blocks.Free(tile);
        m_atlasFullLogged = false;
    }

    //! No warning of its own: rows and tiles are equal in number and taken together, so the
    //! atlas runs out first and AllocateTile has already said so.
    uint32_t ShadowViewSystem::AllocateViewIndex()
    {
        for (uint32_t i = 0; i < kShadowViewCapacity; ++i)
        {
            if (!m_viewRows.test(i))
            {
                m_viewRows.set(i);
                return i;
            }
        }
        return kInvalidShadowSlot;
    }

    void ShadowViewSystem::ReleaseViewIndex(uint32_t index)
    {
        if (index < kShadowViewCapacity)
        {
            m_viewRows.set(index, false);
        }
    }
}
