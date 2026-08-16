#include "ShadowViewSystem.h"

#include <EASTL/fixed_vector.h>
#include <EASTL/sort.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Math/Bit.h>
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
        //! NDC [-1,1] lands on. Per level, since the tile a light is granted is no longer
        //! one fixed size — snapping to a grid that does not match the texels it was
        //! actually given puts the crawl straight back.
        float DirectionalTexelSize(uint32_t level)
        {
            return 2.0f * kDirectionalHalfExtent / static_cast<float>(ShadowUsableTexels(level));
        }

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

        bool ProducesShadowView(const Light::LightRenderData& rd)
        {
            return rd.m_castShadow;
        }

        //! face = axis * 2 + negative. Lib/Lights.hlsli derives the same encoding from the
        //! vector it is shading, and the two must agree — nothing else about a face does.
        //! Its up vector in particular is free, since each face's orientation is baked into
        //! the matrix the shader samples with.
        Math::Vector3 CubeFaceDirection(uint32_t face)
        {
            Math::Vector3 dir(0.0f, 0.0f, 0.0f);
            dir[face >> 1] = (face & 1u) ? -1.0f : 1.0f;
            return dir;
        }

        uint32_t ShadowFaceCount(const Light::LightRenderData& rd)
        {
            // A spot wider than 90 degrees wants faces too — see TODO §八. It stays on its
            // own single view until the point light path is proven.
            return rd.m_type == Light::LightType::Point ? kShadowCubeFaceCount : 1;
        }

        //! The five points bounding a 90 degree face: the light, and the far cap's corners.
        //! A cap at distance range is range wide to each side.
        void FaceHull(const Math::Vector3& origin, uint32_t face, float range,
            Math::Vector3 (&out)[5])
        {
            const Math::Vector3 axis  = CubeFaceDirection(face);
            const Math::Vector3 up    = StableUp(axis);
            const Math::Vector3 right = Math::Cross(up, axis);

            out[0] = origin;
            out[1] = origin + (axis + right + up) * range;
            out[2] = origin + (axis + right - up) * range;
            out[3] = origin + (axis - right + up) * range;
            out[4] = origin + (axis - right - up) * range;
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

        //! Screen radius, in NDC half heights, at which a light earns each level — indexed BY
        //! level, so entry [1] is what 2048 costs, and the finest level is free to anyone who
        //! cleared kScoreEnter at all.
        //!
        //! Geometric rather than evenly spaced: a level up is four times the texels, so it
        //! takes twice the coverage to deserve one. Expressed in NDC and not derived through
        //! an assumed viewport height — the thresholds ARE the policy, and routing them
        //! through a pixel count nobody maintains would only dress up a judgement call.
        constexpr float kLevelPromote[] = { 0.0f, 0.50f, 0.25f, 0.0f };

        //! And what it takes to keep one. Same 0.67 ratio as kScoreEnter to kScoreExit,
        //! reused rather than reinvented so the system has one hysteresis width.
        constexpr float kLevelDemote[] = { 0.0f, 0.34f, 0.17f, 0.0f };

        static_assert(ArraySize(kLevelPromote) > kShadowFinestLevel,
            "one promote threshold per level");
        static_assert(ArraySize(kLevelDemote) > kShadowFinestLevel,
            "one demote threshold per level");

        //! The level a score asks for outright, ignoring whatever the light holds now.
        //! kScoreMax saturates at the coarsest level by falling through the first test, so a
        //! directional light needs no branch of its own here either.
        uint32_t LevelForScore(float score)
        {
            for (uint32_t level = kShadowCoarsestLevel; level < kShadowFinestLevel; ++level)
            {
                if (score >= kLevelPromote[level])
                {
                    return level;
                }
            }
            return kShadowFinestLevel;
        }

        //! The level to ask for, with the light's CURRENT level as the hysteresis state. That
        //! state is read back off the tile the light holds rather than tracked, so this keeps
        //! 6a's property of carrying nothing across frames.
        uint32_t RequestedLevel(float score, uint32_t currentLevel)
        {
            const uint32_t wanted = LevelForScore(score);
            if (currentLevel == kNoShadowLevel || wanted < currentLevel)
            {
                // Holding nothing, or the score cleared the bar for a coarser tile outright.
                return wanted;
            }
            if (score < kLevelDemote[currentLevel])
            {
                // Fallen out of the band. wanted is necessarily finer than currentLevel here,
                // since the demote threshold sits below the promote one for the same level.
                return wanted;
            }
            // Inside the band around what it already has — leave it exactly there, which is
            // what stops a light sitting on a boundary from reallocating every frame.
            return currentLevel;
        }

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
            Entity m_light     = NullEntity;
            float  m_score     = 0.0f;
            bool   m_holdsTile = false;

            //! What the score asks for going in, revised down by the budget pass to what the
            //! ranks above it left affordable. Still the level that will be requested from the
            //! allocator either way.
            uint32_t m_requestedLevel = kShadowFinestLevel;

            //! Bit per face that survived culling. Bit 0 for a light with a single view.
            uint32_t m_faceMask = 1u;
        };

        float RankOf(const Candidate& c)
        {
            return c.m_score * (c.m_holdsTile ? kIncumbentBonus : 1.0f);
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
            const Math::Vector3& focus, const Math::Vector3& dir, const Math::Vector3& up,
            float texelSize)
        {
            const Math::Matrix4X4 basis = Math::LookAt(Math::Vector3(0.0f, 0.0f, 0.0f), dir, up);

            Math::Vector3 lightSpace = Math::Vector3(basis * Math::Vector4(focus, 1.0f));
            lightSpace.x = Math::Floor(lightSpace.x / texelSize) * texelSize;
            lightSpace.y = Math::Floor(lightSpace.y / texelSize) * texelSize;

            return Math::Vector3(Math::Inverse(basis) * Math::Vector4(lightSpace, 1.0f));
        }

        //! A directional light has no position, so the box's placement is chosen rather than
        //! read: it follows the camera, since that is the only region whose shadows are seen.
        //! The eye is pulled back along the light direction so that casters standing between
        //! the light and that region still fall inside the near plane.
        void WriteDirectionalView(View& view, const Light::LightRenderData& rd,
            const Math::Vector3& cameraPos, uint32_t level)
        {
            const Math::Vector3 dir   = Math::Normalize(rd.m_worldDirection);
            const Math::Vector3 up    = StableUp(dir);
            const Math::Vector3 focus =
                SnapToTexelGrid(cameraPos, dir, up, DirectionalTexelSize(level));

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

        //! Wider than 90 degrees by enough that the 45 degree face edge lands `margin` texels
        //! inside the tile instead of on its border. The strip between the two carries the
        //! neighbouring face's content, so a filter tap that walks off the edge reads the
        //! depth that direction really has rather than a clamped copy of the last texel —
        //! which is what the seam across a cube's faces is.
        //!
        //! Shading still picks a face by the exact 90 degree rule, so the strip is only ever
        //! read as filter footprint, never selected into.
        //! A world offset displaces a lookup at the tile edge two ways at once: laterally, and
        //! through the perspective divide on its depth component. The two draw on orthogonal
        //! components of the normal, so the worst case over orientations is sec(halfFov) —
        //! exactly this at 45 degrees, and 1.3% under it at the padded angle, which is 0.04
        //! texels of margin.
        constexpr float kNormalOffsetPerspectiveGain = 1.41421356f;

        float PointFaceFov(const Light::LightRenderData& rd, uint32_t level)
        {
            // Both things that displace a lookup, since either one alone leaves the seam. Half
            // the footprint is how far the filter reaches, bilinear widens every tap by half a
            // texel more, and the normal offset moves the lookup before the filter even runs.
            const float margin =
                0.5f * static_cast<float>(Light::ShadowFilterFootprint(rd.m_shadowFilterWidth)) + 1.0f
                + rd.m_shadowNormalOffsetTexels * kNormalOffsetPerspectiveGain;

            // The 45 degree edge is to sit at NDC 1 - 2*margin/texels, and a direction at that
            // angle maps to 1/tan(halfFov), which gives the half angle outright.
            const float texels = static_cast<float>(ShadowUsableTexels(level));
            const float inner  = Math::Clamp(texels - 2.0f * margin, 1.0f, texels);
            return 2.0f * Math::Atan(texels / inner);
        }

        void WritePointFaceView(View& view, const Light::LightRenderData& rd, uint32_t face,
            uint32_t level)
        {
            const Math::Vector3 dir = CubeFaceDirection(face);
            view.m_worldToView = Math::LookAt(
                rd.m_worldPosition, rd.m_worldPosition + dir, StableUp(dir));

            const float farZ  = rd.m_range > kSpotNearZ ? rd.m_range : kSpotNearZ * 2.0f;
            view.m_viewToClip = Math::PerspectiveFov(PointFaceFov(rd, level), 1.0f, kSpotNearZ, farZ);
        }
    }

    void ShadowViewSystem::Init(RHI::RHIContext& rhiCtx)
    {
        m_atlas.Init(rhiCtx);
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
        if (!IsResourceReady(*rhiCtx, m_atlas.Image()))
        {
            return;
        }

        // Sweep: a light that died, lost its render data or stopped casting hands its views
        // and its tiles back. So does one whose type changed, since the number of views was
        // fixed when they were created — an author switching a light to Point in the editor
        // would otherwise leave it with the single view it had as a directional.
        eastl::fixed_vector<Entity, 8> orphans;
        world->GetView<ShadowViewRefs>().each([&](Entity e, const ShadowViewRefs& refs)
        {
            const auto* rd = world->TryGet<Light::LightRenderData>(e);
            if (!world->Has<DeadTag>(e) && rd && ProducesShadowView(*rd)
                && refs.m_views.size() == ShadowFaceCount(*rd))
            {
                return;
            }

            ReleaseAllocation(*rhiCtx, refs);
            for (RHI::RHIHandle v : refs.m_views)
            {
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

            const auto* refs      = world->TryGet<ShadowViewRefs>(e);
            const bool  holdsTile = refs && refs->m_baseIndex >= 0;

            const uint32_t grantedLevel = holdsTile ? GrantedLevel(*rhiCtx, *refs) : kNoShadowLevel;

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
                if (score < (holdsTile ? kScoreExit : kScoreEnter))
                {
                    Deactivate(*world, *rhiCtx, e);
                    return;
                }
            }

            // Which faces are worth a tile. A light with one view has no face to reject, and
            // with no main view nothing is rejected at all — the same direction 6a errs in.
            uint32_t       faceMask  = 1u;
            const uint32_t faceCount = ShadowFaceCount(rd);
            if (faceCount > 1 && main.m_valid)
            {
                faceMask = 0;
                for (uint32_t face = 0; face < faceCount; ++face)
                {
                    Math::Vector3 hull[5];
                    FaceHull(rd.m_worldPosition, face, rd.m_range, hull);
                    if (!main.m_frustum.RejectsHull(hull, 5))
                    {
                        faceMask = SetBit(faceMask, face);
                    }
                }
                if (faceMask == 0)
                {
                    Deactivate(*world, *rhiCtx, e);
                    return;
                }
            }
            else if (faceCount > 1)
            {
                faceMask = BIT_MASK(faceCount);
            }

            candidates.push_back(
                Candidate{ e, score, holdsTile, RequestedLevel(score, grantedLevel), faceMask });
        });

        eastl::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return RankOf(a) > RankOf(b); });

        // Commit the atlas down the ranking, reducing the level of whoever cannot be afforded
        // at the size they asked for. Two things fall out of doing this before allocating
        // rather than during: the admitted set is known BEFORE any tile changes hands, which
        // is what lets the rejected lights be deactivated first and their space reused this
        // same frame; and a light that does not fit is given a smaller tile rather than
        // skipped, so no light's shadow hinges on another light's score.
        //
        // The sum is optimistic — it ignores fragmentation, so the tiles may not actually cut
        // even when the area adds up. That error lands in the safe direction: allocation
        // reduces the level again, and no light is rejected that could have been served.
        uint32_t committedUnits = 0;
        size_t   admittedCount  = 0;
        for (; admittedCount < candidates.size(); ++admittedCount)
        {
            const uint32_t faces = CountBitsSet(candidates[admittedCount].m_faceMask);

            uint32_t level = candidates[admittedCount].m_requestedLevel;
            while (level <= kShadowFinestLevel
                && committedUnits + faces * ShadowTileCost(level) > kShadowBudgetUnits)
            {
                ++level;
            }
            if (level > kShadowFinestLevel)
            {
                // Not even the finest tile is left, and the finest is the atom — so nothing
                // further down the ranking can fit either. Stopping here is exact rather than
                // a heuristic, and it keeps the ranking monotone.
                break;
            }
            candidates[admittedCount].m_requestedLevel = level;
            committedUnits += faces * ShadowTileCost(level);
        }

        // Rejected first: a tile handed back here is available to an admitted light below in
        // the same frame.
        for (size_t i = admittedCount; i < candidates.size(); ++i)
        {
            Deactivate(*world, *rhiCtx, candidates[i].m_light);
        }
        for (size_t i = 0; i < admittedCount; ++i)
        {
            Activate(*world, *rhiCtx, candidates[i].m_light, candidates[i].m_requestedLevel,
                candidates[i].m_faceMask, main.m_eye);
        }
    }

    //! Every face of a light shares one level, so the first tile found answers for all.
    uint32_t ShadowViewSystem::GrantedLevel(RHI::RHIContext& rhiCtx, const ShadowViewRefs& refs)
    {
        for (RHI::RHIHandle view : refs.m_views)
        {
            if (const auto* tile = rhiCtx.TryGet<ShadowAtlasTile>(view))
            {
                return ShadowAtlasAllocator::LevelOfTile(tile->m_tile);
            }
        }
        return kNoShadowLevel;
    }

    uint32_t ShadowViewSystem::HeldFaceMask(RHI::RHIContext& rhiCtx, const ShadowViewRefs& refs)
    {
        uint32_t mask = 0;
        for (uint32_t face = 0; face < refs.m_views.size(); ++face)
        {
            if (rhiCtx.Has<ShadowAtlasTile>(refs.m_views[face]))
            {
                mask = SetBit(mask, face);
            }
        }
        return mask;
    }

    void ShadowViewSystem::ReleaseTilesKeepingRows(
        RHI::RHIContext& rhiCtx, const ShadowViewRefs& refs)
    {
        for (RHI::RHIHandle view : refs.m_views)
        {
            if (const auto* tile = rhiCtx.TryGet<ShadowAtlasTile>(view))
            {
                m_atlas.ReleaseTile(tile->m_tile);
            }
            rhiCtx.Remove<ShadowAtlasTile>(view);
        }
    }

    void ShadowViewSystem::ReleaseAllocation(
        RHI::RHIContext& rhiCtx, const ShadowViewRefs& refs)
    {
        ReleaseTilesKeepingRows(rhiCtx, refs);
        for (RHI::RHIHandle view : refs.m_views)
        {
            rhiCtx.Remove<ShadowViewIndex>(view);
        }
        if (refs.m_baseIndex >= 0)
        {
            m_atlas.ReleaseRows(static_cast<uint32_t>(refs.m_baseIndex),
                static_cast<uint32_t>(refs.m_views.size()));
        }
    }

    //! Every surviving face gets a tile of one level, or none of them does. A cube missing a
    //! face leaks a hard-edged wedge whose presence tracks atlas fragmentation rather than
    //! anything in the scene; one level coarser everywhere is the better failure.
    //!
    //! The g_ShadowViews rows are untouched throughout — the light's m_shadowIndex, published
    //! to the shader, survives every change of level and of face set.
    bool ShadowViewSystem::ReallocateTiles(WorldContext& world, RHI::RHIContext& rhiCtx,
        Entity light, const ShadowViewRefs& refs, uint32_t level, uint32_t faceMask)
    {
        const uint32_t currentLevel = GrantedLevel(rhiCtx, refs);
        if (currentLevel == level && HeldFaceMask(rhiCtx, refs) == faceMask)
        {
            return true;
        }

        const uint32_t faces = CountBitsSet(faceMask);
        uint32_t       tiles[kShadowCubeFaceCount] = {};

        if (currentLevel != kNoShadowLevel && level < currentLevel)
        {
            // Promoting: allocate BEFORE releasing. The larger tiles may want the very space
            // the current ones sit in, in which case this fails and the light keeps what it
            // has — a missed promotion, not a lost shadow. Releasing first would make that
            // same case cost the light its tiles, and a light held down by a full atlas would
            // then release and re-acquire every single frame.
            if (!m_atlas.AllocateTilesAt(level, faces, tiles))
            {
                return true;
            }
            ReleaseTilesKeepingRows(rhiCtx, refs);
        }
        else
        {
            // Demoting, or arriving, or only the face set moved: release first, since the
            // space wanted is most likely inside what is being given up.
            ReleaseTilesKeepingRows(rhiCtx, refs);
            if (m_atlas.AllocateTilesOrFiner(level, faces, tiles) == kNoShadowLevel)
            {
                Deactivate(world, rhiCtx, light);
                return false;
            }
        }

        uint32_t next = 0;
        for (uint32_t face = 0; face < refs.m_views.size(); ++face)
        {
            if (CheckBit(faceMask, face))
            {
                rhiCtx.AddOrReplace<ShadowAtlasTile>(
                    refs.m_views[face], ShadowAtlasTile{ tiles[next++] });
            }
        }
        return true;
    }

    void ShadowViewSystem::Activate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light,
        uint32_t level, uint32_t faceMask, const Math::Vector3& focus)
    {
        const auto* rd = world.TryGet<Light::LightRenderData>(light);
        if (!rd)
        {
            return;
        }
        const uint32_t faceCount = ShadowFaceCount(*rd);

        auto* refs = world.TryGet<ShadowViewRefs>(light);
        if (!refs)
        {
            ShadowViewRefs added;
            for (uint32_t face = 0; face < faceCount; ++face)
            {
                const RHI::RHIHandle created = CreateViewEntity<ShadowViewTag>(rhiCtx);
                if (created == RHI::NullHandle)
                {
                    for (RHI::RHIHandle view : added.m_views)
                    {
                        DestroyViewEntity(rhiCtx, view);
                    }
                    return;
                }
                added.m_views.push_back(created);
            }
            refs = &world.Add<ShadowViewRefs>(light, eastl::move(added));
        }

        // Rows come first and all at once, so a face index addresses its row whether or not
        // that face is holding a tile this frame.
        if (refs->m_baseIndex < 0)
        {
            const uint32_t row = m_atlas.AllocateRows(faceCount);
            if (row == kInvalidShadowSlot)
            {
                return;
            }
            refs->m_baseIndex = static_cast<int32_t>(row);
            for (uint32_t face = 0; face < faceCount; ++face)
            {
                rhiCtx.AddOrReplace<ShadowViewIndex>(
                    refs->m_views[face], ShadowViewIndex{ row + face });
            }
            LOG_INFO("[ShadowViewSystem] Light {} took {} shadow view row(s) from {}.",
                static_cast<uint32_t>(light), faceCount, row);
        }

        if (!ReallocateTiles(world, rhiCtx, light, *refs, level, faceMask))
        {
            return;
        }

        for (uint32_t face = 0; face < faceCount; ++face)
        {
            const RHI::RHIHandle viewHandle = refs->m_views[face];

            const auto* tile = rhiCtx.TryGet<ShadowAtlasTile>(viewHandle);
            if (!tile)
            {
                if (!rhiCtx.Has<ViewInactiveTag>(viewHandle))
                {
                    rhiCtx.Add<ViewInactiveTag>(viewHandle);
                }
                continue;
            }
            rhiCtx.Remove<ViewInactiveTag>(viewHandle);

            View& view  = rhiCtx.Get<View>(viewHandle);
            view.m_rect = ShadowTileRect(tile->m_tile);

            const uint32_t tileLevel = ShadowAtlasAllocator::LevelOfTile(tile->m_tile);

            switch (rd->m_type)
            {
            case Light::LightType::Directional:
                WriteDirectionalView(view, *rd, focus, tileLevel);
                break;
            case Light::LightType::Point:
                WritePointFaceView(view, *rd, face, tileLevel);
                break;
            default:
                WriteSpotView(view, *rd);
                break;
            }
        }
    }

    void ShadowViewSystem::Deactivate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light)
    {
        auto* refs = world.TryGet<ShadowViewRefs>(light);
        if (!refs || refs->m_baseIndex < 0)
        {
            return;
        }

        ReleaseAllocation(rhiCtx, *refs);
        for (RHI::RHIHandle v : refs->m_views)
        {
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
        m_atlas.Shutdown(rhiCtx);

        // Strip the world-side refs so a re-init starts clean.
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<ShadowViewRefs>();
        }
    }
}
