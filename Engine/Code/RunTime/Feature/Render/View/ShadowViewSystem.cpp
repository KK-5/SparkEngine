#include "ShadowViewSystem.h"

#include <EASTL/fixed_vector.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Math/Bit.h>
#include <Math/Frustum.h>
#include <Math/MathUtils.h>
#include <Math/Sphere.h>

#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/ResourceBuilder.h>   // IsResourceReady

#include <Light/Components.h>

#include "View.h"
#include "ViewComponents.h"
#include "ViewFactory.h"
#include "ViewTags.h"

namespace Spark::Render
{
    namespace
    {
        //! World size of one shadow texel of a directional light's box. Per level, since the
        //! resolution a light is granted is not one fixed size — snapping to a grid that does not
        //! match the texels it was actually given puts the crawl straight back.
        float DirectionalTexelSize(float radius, uint32_t level)
        {
            return 2.0f * radius / static_cast<float>(ShadowResolution(level));
        }

        //! A shadow distance below this is not worth a view. Also keeps the slice non-empty
        //! when someone authors a distance inside the camera's own near plane.
        constexpr float kMinShadowDistance = 1.0f;

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

        //! A cube light's shadow map i is its face i.
        uint32_t ShadowMapCount(const Light::LightRenderData& rd)
        {
            // A spot wider than 90 degrees wants faces too — see TODO §八. It stays on its
            // own single view until the point light path is proven.
            //
            // A rect shines into a whole hemisphere, which no single view covers, so it
            // borrows the point light's cube and is shadowed as a point at its centre —
            // right in the umbra, wrong in the penumbra (TODO_AreaLightPlan.md §五).
            const bool cube = rd.m_type == Light::LightType::Point
                           || rd.m_type == Light::LightType::Rect;
            return cube ? kShadowCubeFaceCount : 1;
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

        //! Whether a face looks only at what lies behind a rect's emitting plane, where it
        //! lights nothing. The apex sits on the plane, so the four far corners decide it.
        bool FaceBehindEmitter(const Light::LightRenderData& rd, const Math::Vector3 (&hull)[5])
        {
            if (rd.m_type != Light::LightType::Rect)
            {
                return false;
            }
            for (uint32_t corner = 1; corner < 5; ++corner)
            {
                if (Math::Dot(hull[corner] - rd.m_worldPosition, rd.m_worldDirection) > 0.0f)
                {
                    return false;
                }
            }
            return true;
        }

        //! A light must cover kCoverageEnter of the view to take a shadow but only kCoverageExit
        //! to keep one, so one hovering at the boundary does not flicker on and off.
        constexpr float kCoverageEnter = 0.03f;
        constexpr float kCoverageExit  = 0.02f;

        //! A directional light, which has no bounding volume to project and lights everything
        //! there is: it takes the coarsest level.
        //!
        //! NOT for a radius that merely could not be computed: answering "unmeasurable" with
        //! "maximal" hands the largest shadow maps to a light whose size nobody knows.
        constexpr float kCoverageDirectional = 1e9f;

        //! What a light covering the entire view covers. The unit is NDC half heights, so 1.0
        //! IS the whole view — anything beyond it is off screen and buys no more detail.
        constexpr float kCoverageFullView = 1.0f;

        //! Screen radius, in NDC half heights, at which a light earns each level — indexed BY
        //! level, so entry [1] is what 2048 costs, and the finest level is free to anyone who
        //! cleared kCoverageEnter at all.
        //!
        //! Geometric rather than evenly spaced: a level up is four times the texels, so it
        //! takes twice the coverage to deserve one. Expressed in NDC and not derived through
        //! an assumed viewport height — the thresholds ARE the policy, and routing them
        //! through a pixel count nobody maintains would only dress up a judgement call.
        constexpr float kLevelPromote[] = { 0.0f, 0.50f, 0.25f, 0.0f };

        //! And what it takes to keep one. Same 0.67 ratio as kCoverageEnter to kCoverageExit,
        //! reused rather than reinvented so the system has one hysteresis width.
        constexpr float kLevelDemote[] = { 0.0f, 0.34f, 0.17f, 0.0f };

        static_assert(ArraySize(kLevelPromote) > kShadowFinestLevel,
            "one promote threshold per level");
        static_assert(ArraySize(kLevelDemote) > kShadowFinestLevel,
            "one demote threshold per level");

        //! The level a coverage asks for outright, ignoring whatever the light holds now.
        //! kCoverageDirectional saturates at the coarsest level by falling through the first
        //! test, so a directional light needs no branch of its own here either.
        uint32_t LevelForCoverage(float coverage)
        {
            for (uint32_t level = kShadowCoarsestLevel; level < kShadowFinestLevel; ++level)
            {
                if (coverage >= kLevelPromote[level])
                {
                    return level;
                }
            }
            return kShadowFinestLevel;
        }

        //! The level to ask for, with the light's CURRENT level (ShadowMaps::m_level) as the
        //! hysteresis state.
        uint32_t RequestedLevel(float coverage, uint32_t currentLevel)
        {
            const uint32_t wanted = LevelForCoverage(coverage);
            if (currentLevel == kNoShadowLevel || wanted < currentLevel)
            {
                // Holding nothing, or the coverage cleared the bar for a coarser level outright.
                return wanted;
            }
            if (coverage < kLevelDemote[currentLevel])
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
            Math::Vector3 m_forward {0.0f, 0.0f, 1.0f};
            Math::Frustum m_frustum {};
            float         m_proj11 = 1.0f;   // 1 / tan(fovY / 2)
            float         m_aspect = 1.0f;
            float         m_nearZ  = 0.1f;

            //! False during warmup and with no camera, and then nothing is culled: letting
            //! through what should be rejected costs a shadow map, rejecting what should pass
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

                const Math::Matrix4X4 viewToWorld = Math::Inverse(view.m_worldToView);
                info.m_eye     = Math::Vector3(viewToWorld[3]);
                info.m_forward = Math::Normalize(Math::Vector3(viewToWorld[2]));

                // Read back rather than plumbed through: a View carries its matrices and
                // nothing else, and every projection this engine builds is LH_ZO reversed-Z,
                // where [0][0] and [1][1] are the reciprocal half-extents and the third column
                // holds -near/(far-near) and near*far/(far-near). NDC z is 1 at the near plane,
                // so near = [3][2] / (1 - [2][2]).
                const Math::Matrix4X4& proj = view.m_viewToClip;
                info.m_proj11 = proj[1][1];
                if (proj[0][0] != 0.0f)
                {
                    info.m_aspect = proj[1][1] / proj[0][0];
                }
                if (proj[2][2] != 1.0f)
                {
                    info.m_nearZ = proj[3][2] / (1.0f - proj[2][2]);
                }

                if (const auto* frustum = rhiCtx.TryGet<ViewFrustum>(e))
                {
                    info.m_frustum = frustum->m_frustum;
                    info.m_valid   = true;
                }
            });
            return info;
        }

        //! Smallest sphere containing the camera frustum between its near plane and
        //! shadowDistance. A SPHERE and not the slice's box, because a box fitted to the
        //! frustum changes size as the camera rotates — and with it the texel size, leaving
        //! SnapToTexelGrid no fixed grid to quantize against. A sphere containing a rotating
        //! frustum is the same sphere.
        //!
        //! Its radius is a constant while fov, aspect and shadow distance are: only the
        //! centre travels with the camera. A change to any of the three does resize it, and
        //! the texel grid jumps once as it does.
        Math::Sphere FrustumSliceSphere(const MainViewInfo& main, float shadowDistance)
        {
            const float n     = main.m_nearZ;
            const float floor = n + kMinShadowDistance;
            const float f     = shadowDistance > floor ? shadowDistance : floor;

            // Half-diagonal of the slice per unit of depth: the corner directions are
            // (±aspect*t, ±t, 1) with t = tan(fovY/2), so k is the length of their xy part.
            const float t  = (main.m_proj11 != 0.0f) ? (1.0f / main.m_proj11) : 1.0f;
            const float k2 = t * t * (1.0f + main.m_aspect * main.m_aspect);

            float centerDist;
            float radius;
            if (k2 * (f + n) >= f - n)
            {
                // The slice flares faster than it is deep, so the far cap's corners already
                // enclose the near ones and the sphere is that cap's circumcircle.
                centerDist = f;
                radius     = f * Math::Sqrt(k2);
            }
            else
            {
                // Equidistant from both caps' corners: solving |corner(n)| = |corner(f)| for
                // a centre on the view axis.
                const float d = f - n;
                const float s = f + n;
                centerDist    = 0.5f * s * (1.0f + k2);
                radius        = 0.5f * Math::Sqrt(d * d + 2.0f * k2 * (f * f + n * n) + s * s * k2 * k2);
            }

            return Math::Sphere{ main.m_eye + main.m_forward * centerDist, radius };
        }

        //! The volume a light's shadows must cover. Only a directional light has one to
        //! choose — every other type is bounded by its own range, and its view is built from
        //! that instead.
        Math::Sphere ShadowVolume(const Light::LightRenderData& rd, const MainViewInfo& main)
        {
            if (rd.m_type != Light::LightType::Directional)
            {
                return Math::Sphere{};
            }
            return FrustumSliceSphere(main, rd.m_shadowDistance);
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
                // Inside the sphere the tangent cone has no apex angle to measure, but the
                // answer is not unknown: the light surrounds the viewer, so it covers the view.
                return kCoverageFullView;
            }
            return sphere.radius * main.m_proj11 / Math::Sqrt(d2 - r2);
        }

        //! Quantize the box origin to whole texels in the light's own frame. Without it the
        //! texel grid slides continuously under static geometry as the camera moves and
        //! shadow edges crawl. Only x/y are snapped — z runs along the light, where the grid
        //! does not live.
        //!
        //! This works only because the box size is fixed, which is exactly what fitting the
        //! camera frustum with a SPHERE buys: a box fitted to the slice directly would
        //! change size as the camera turns, leaving no constant grid to quantize against.
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
        //! read: it wraps the sphere of camera frustum whose shadows are seen at all.
        //!
        //! ONE radius defines the whole view — the box is 2r across and 2r deep, and the eye
        //! sits exactly on the sphere. Depth is not a free dimension to be padded: it is the
        //! same 2r the xy extent is, which is what makes a bias in NDC depth and an offset in
        //! texels the same currency (see ShadowViewData::m_depthBias). Anything that pushes
        //! the eye further back to catch a caster stretches depth without stretching xy and
        //! breaks that — casters in front of the near plane are handled by turning depth
        //! clipping off in ShadowPass instead, which costs the depth range nothing.
        void WriteDirectionalView(View& view, const Light::LightRenderData& rd,
            const Math::Sphere& volume, uint32_t level)
        {
            const Math::Vector3 dir = Math::Normalize(rd.m_worldDirection);
            const Math::Vector3 up  = StableUp(dir);

            const float texelSize = DirectionalTexelSize(volume.radius, level);
            const Math::Vector3 center = SnapToTexelGrid(volume.center, dir, up, texelSize);

            view.m_worldToView = Math::LookAt(center - dir * volume.radius, center, up);
            view.m_viewToClip  = Math::OrthographicProjection(
                -volume.radius, volume.radius, -volume.radius, volume.radius,
                0.0f, 2.0f * volume.radius);
        }

        void WriteSpotView(View& view, const Light::LightRenderData& rd)
        {
            const Math::Vector3 dir = Math::Normalize(rd.m_worldDirection);
            view.m_worldToView = Math::LookAt(rd.m_worldPosition, rd.m_worldPosition + dir, StableUp(dir));

            // m_cosOuter is the cosine of the HALF angle, so the full vertical fov is twice
            // its arccos. Square shadow map, hence aspect 1.
            const float outerHalf = Math::Clamp(
                Math::Acos(Math::Clamp(rd.m_cosOuter, -1.0f, 1.0f)),
                Math::Radians(kSpotMinHalfAngleDeg), Math::Radians(kSpotMaxHalfAngleDeg));
            const float farZ      = rd.m_range > kSpotNearZ ? rd.m_range : kSpotNearZ * 2.0f;
            view.m_viewToClip     = Math::PerspectiveFov(outerHalf * 2.0f, 1.0f, kSpotNearZ, farZ);
        }

        //! Wider than 90 degrees by enough that the 45 degree face edge lands `margin` texels
        //! inside the shadow map instead of on its border. The strip between the two carries the
        //! neighbouring face's content, so a filter tap that walks off the edge reads the
        //! depth that direction really has rather than a clamped copy of the last texel —
        //! which is what the seam across a cube's faces is.
        //!
        //! Shading still picks a face by the exact 90 degree rule, so the strip is only ever
        //! read as filter footprint, never selected into.
        //! A world offset displaces a lookup at the shadow map edge two ways at once: laterally, and
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
            const float texels = static_cast<float>(ShadowResolution(level));
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
        m_maps.Init(rhiCtx, *Service<RHI::RHIInterface>::Get()->GetDevice());
    }

    void ShadowViewSystem::Update()
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx)
        {
            return;
        }

        // The single readiness gate for shadows. Holding shadow maps back until they exist
        // leaves every m_shadowIndex at -1, which is what the lighting shader already tests,
        // so nothing downstream needs a second check.
        if (!RHI::IsResourceReady(*rhiCtx, m_maps.GetResource()))
        {
            return;
        }

        // Sweep: a light that died, lost its render data or stopped casting hands its views
        // and its shadow maps back. So does one whose type changed, since the number of views was
        // fixed when they were created — an author switching a light to Point in the editor
        // would otherwise leave it with the single view it had as a directional.
        eastl::fixed_vector<Entity, 8> orphans;
        world->GetView<ShadowViewRefs>().each([&](Entity e, ShadowViewRefs& refs)
        {
            const auto* rd = world->TryGet<Light::LightRenderData>(e);
            if (!world->Has<DeadTag>(e) && rd && ProducesShadowView(*rd)
                && refs.m_views.size() == ShadowMapCount(*rd))
            {
                return;
            }

            ReleaseMaps(*world, e, refs);
            LOG_INFO("[ShadowViewSystem] Light {} released {} shadow view(s).",
                static_cast<uint32_t>(e), refs.m_views.size());
            orphans.push_back(e);
        });
        world->Remove<ShadowViewRefs>(orphans.begin(), orphans.end());

        const MainViewInfo main = ResolveMainView(*rhiCtx);

        // First come, first served: a light finding every shadow map taken goes without.
        world->GetView<Light::LightRenderData>(Exclude<DeadTag>).each(
            [&](Entity e, const Light::LightRenderData& rd)
        {
            if (!ProducesShadowView(rd))
            {
                return;
            }

            const auto*    maps         = world->TryGet<ShadowMaps>(e);
            const bool     holdsShadow  = maps != nullptr;
            const uint32_t grantedLevel = holdsShadow ? maps->m_level : kNoShadowLevel;

            // No LightBounds means unbounded influence, so nothing here can reject it.
            float coverage = kCoverageDirectional;
            if (const auto* bounds = world->TryGet<Light::LightBounds>(e); bounds && main.m_valid)
            {
                if (!main.m_frustum.IntersectsSphere(bounds->m_sphere.center, bounds->m_sphere.radius))
                {
                    Deactivate(*world, *rhiCtx, e);
                    return;
                }
                coverage = ScreenRadius(bounds->m_sphere, main);
                if (coverage < (holdsShadow ? kCoverageExit : kCoverageEnter))
                {
                    Deactivate(*world, *rhiCtx, e);
                    return;
                }
            }

            // Which shadow maps are worth having: a cube's faces are culled. A light with one view
            // has nothing to reject, and with no main view the frustum rejects nothing — the same
            // direction 6a errs in.
            uint32_t       mapMask  = 1u;
            const uint32_t mapCount = ShadowMapCount(rd);
            if (mapCount > 1)
            {
                mapMask = 0;
                for (uint32_t face = 0; face < mapCount; ++face)
                {
                    Math::Vector3 hull[5];
                    FaceHull(rd.m_worldPosition, face, rd.m_range, hull);
                    if (FaceBehindEmitter(rd, hull)
                        || (main.m_valid && main.m_frustum.RejectsHull(hull, 5)))
                    {
                        continue;
                    }
                    mapMask = SetBit(mapMask, face);
                }
                if (mapMask == 0)
                {
                    Deactivate(*world, *rhiCtx, e);
                    return;
                }
            }

            Activate(*world, *rhiCtx, e, RequestedLevel(coverage, grantedLevel), mapMask,
                ShadowVolume(rd, main));
        });
    }

    uint32_t ShadowViewSystem::CommitMaps(ShadowMaps& maps, uint32_t level, uint32_t mapMask, uint32_t mapCount)
    {
        uint32_t committed = 0;
        for (uint32_t i = 0; i < mapCount; ++i)
        {
            const uint32_t index = maps.First() + i;
            if (!CheckBit(mapMask, i))
            {
                m_maps.Decommit(index);
            }
            else if (m_maps.Commit(index, ShadowResolution(level)))
            {
                committed = SetBit(committed, i);
            }
        }
        maps.m_level = level;
        return committed;
    }

    void ShadowViewSystem::ReleaseMaps(WorldContext& world, Entity light, const ShadowViewRefs& refs)
    {
        const auto* maps = world.TryGet<ShadowMaps>(light);
        if (!maps)
        {
            return;
        }
        for (uint32_t i = 0; i < refs.m_views.size(); ++i)
        {
            m_maps.Decommit(maps->First() + i);
        }
        world.Remove<ShadowMaps>(light);
    }

    void ShadowViewSystem::Activate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light,
        uint32_t level, uint32_t mapMask, const Math::Sphere& volume)
    {
        const auto* rd = world.TryGet<Light::LightRenderData>(light);
        if (!rd)
        {
            return;
        }
        const uint32_t mapCount = ShadowMapCount(*rd);

        auto* refs = world.TryGet<ShadowViewRefs>(light);
        if (!refs)
        {
            ShadowViewRefs added;
            for (uint32_t i = 0; i < mapCount; ++i)
            {
                RHI::UniqueRHIHandle created = CreateViewEntity<ShadowViewTag>(rhiCtx);
                if (!created.IsValid())
                {
                    return;
                }
                added.m_views.push_back(eastl::move(created));
            }
            refs = &world.Add<ShadowViewRefs>(light, eastl::move(added));
        }

        // Shadow maps come all at once, so a view addresses its own whether or not it is
        // committed this frame.
        auto* maps = world.TryGet<ShadowMaps>(light);
        if (!maps)
        {
            Suballocation range = m_maps.AllocateMaps(mapCount);
            if (!range.IsValid())
            {
                if (!m_outOfMapsLogged)
                {
                    LOG_WARN("[ShadowViewSystem] All {} shadow maps are taken; light {} casts no shadow "
                             "until some are given back.", kShadowMapCount, static_cast<uint32_t>(light));
                    m_outOfMapsLogged = true;
                }
                return;
            }
            m_outOfMapsLogged = false;
            maps = &world.Add<ShadowMaps>(light, ShadowMaps{ eastl::move(range), kNoShadowLevel });
            LOG_INFO("[ShadowViewSystem] Light {} took {} shadow map(s) from {}.",
                static_cast<uint32_t>(light), mapCount, maps->First());
        }

        // A view whose shadow map could not be committed casts no shadow this frame, and is
        // tried again the next.
        const uint32_t committed = CommitMaps(*maps, level, mapMask, mapCount);

        for (uint32_t i = 0; i < mapCount; ++i)
        {
            const RHI::RHIHandle viewHandle = refs->m_views[i].Get();
            if (!CheckBit(committed, i))
            {
                if (!rhiCtx.Has<ViewInactiveTag>(viewHandle))
                {
                    rhiCtx.Add<ViewInactiveTag>(viewHandle);
                }
                continue;
            }
            rhiCtx.Remove<ViewInactiveTag>(viewHandle);

            // The top left of its layer. Field order is (minX, maxX, minY, maxY).
            const float extent = ShadowMapExtent(level);
            View& view         = rhiCtx.Get<View>(viewHandle);
            view.m_rect        = ViewRect{ 0.0f, extent, 0.0f, extent };
            view.m_bufferSize  = Math::Vector2Int(kShadowMapResolution, kShadowMapResolution);

            switch (rd->m_type)
            {
            case Light::LightType::Directional:
                WriteDirectionalView(view, *rd, volume, level);
                break;
            case Light::LightType::Point:
            case Light::LightType::Rect:
                WritePointFaceView(view, *rd, i, level);
                break;
            default:
                WriteSpotView(view, *rd);
                break;
            }
        }
    }

    void ShadowViewSystem::Deactivate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light)
    {
        const auto* refs = world.TryGet<ShadowViewRefs>(light);
        if (!refs || !world.Has<ShadowMaps>(light))
        {
            return;
        }

        ReleaseMaps(world, light, *refs);
        for (const RHI::UniqueRHIHandle& v : refs->m_views)
        {
            if (!rhiCtx.Has<ViewInactiveTag>(v.Get()))
            {
                rhiCtx.Add<ViewInactiveTag>(v.Get());
            }
        }

        LOG_INFO("[ShadowViewSystem] Light {} gave its shadow back.", static_cast<uint32_t>(light));
    }

    void ShadowViewSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        // The refs own the views, so clearing them lets the views go too. The indices go before
        // the allocator that hands them out.
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<ShadowMaps>();
            world->Clear<ShadowViewRefs>();
        }
        m_maps.Shutdown(rhiCtx);
    }
}
