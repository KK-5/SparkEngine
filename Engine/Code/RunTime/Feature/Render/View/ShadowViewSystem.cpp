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

        static_assert(sizeof(kLevelPromote) / sizeof(float) > kShadowFinestLevel,
            "one promote threshold per level");
        static_assert(sizeof(kLevelDemote) / sizeof(float) > kShadowFinestLevel,
            "one demote threshold per level");

        //! The light holds no tile, so there is no level to be hysteretic about.
        constexpr uint32_t kNoLevel = ~0u;

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
            if (currentLevel == kNoLevel || wanted < currentLevel)
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

            const auto* refs      = world->TryGet<ShadowViewRefs>(e);
            const bool  holdsTile = refs && refs->m_baseIndex >= 0;

            const uint32_t grantedLevel =
                holdsTile ? GrantedLevel(*rhiCtx, refs->m_views[0]) : kNoLevel;

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
            candidates.push_back(
                Candidate{ e, score, holdsTile, RequestedLevel(score, grantedLevel) });
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
            uint32_t level = candidates[admittedCount].m_requestedLevel;
            while (level <= kShadowFinestLevel
                && committedUnits + ShadowTileCost(level) > kShadowBudgetUnits)
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
            committedUnits += ShadowTileCost(level);
        }

        // Rejected first: a tile handed back here is available to an admitted light below in
        // the same frame.
        for (size_t i = admittedCount; i < candidates.size(); ++i)
        {
            Deactivate(*world, *rhiCtx, candidates[i].m_light);
        }
        for (size_t i = 0; i < admittedCount; ++i)
        {
            Activate(*world, *rhiCtx, candidates[i].m_light,
                candidates[i].m_requestedLevel, main.m_eye);
        }
    }

    uint32_t ShadowViewSystem::GrantedLevel(RHI::RHIContext& rhiCtx, RHI::RHIHandle view)
    {
        const auto* tile = rhiCtx.TryGet<ShadowAtlasTile>(view);
        return tile ? ShadowAtlasAllocator::Decode(tile->m_tile).m_level : kNoLevel;
    }

    //! The g_ShadowViews row is untouched throughout — the light's m_shadowIndex, which is
    //! published to the shader, survives every change of level. That is what separating the
    //! tile id from the row bought.
    bool ShadowViewSystem::ReallocateTile(WorldContext& world, RHI::RHIContext& rhiCtx,
        RHI::RHIHandle view, Entity light, uint32_t level)
    {
        const uint32_t currentTile  = rhiCtx.Get<ShadowAtlasTile>(view).m_tile;
        const uint32_t currentLevel = ShadowAtlasAllocator::Decode(currentTile).m_level;
        if (currentLevel == level)
        {
            return true;
        }

        uint32_t replacement = kInvalidShadowSlot;
        if (level < currentLevel)
        {
            // Promoting: allocate BEFORE releasing. The larger tile may want the very space
            // the current one sits in, in which case this fails and the light keeps what it
            // has — a missed promotion, not a lost shadow. Releasing first would make that
            // same case cost the light its tile, and a light held down by a full atlas would
            // then release and re-acquire every single frame.
            replacement = AllocateTile(level);
            if (replacement == kInvalidShadowSlot)
            {
                return true;   // kept what it had, which is a fine outcome
            }
            ReleaseTile(currentTile);
        }
        else
        {
            // Demoting: release first, since the space for the smaller tile is most likely
            // inside the one being given up — which also makes this allocation certain.
            ReleaseTile(currentTile);
            replacement = AllocateTileOrFiner(level);
            if (replacement == kInvalidShadowSlot)
            {
                // Unreachable: a finer tile can always be split out of the one just returned.
                // Handled anyway, because the alternative is a view still pointing at a tile
                // the allocator has since given to somebody else.
                LOG_ERROR("[ShadowViewSystem] Light {} could not take a level {} tile right "
                          "after returning a level {} one.",
                    static_cast<uint32_t>(light), level, currentLevel);
                rhiCtx.Remove<ShadowAtlasTile>(view);
                Deactivate(world, rhiCtx, light);
                return false;
            }
        }

        rhiCtx.AddOrReplace<ShadowAtlasTile>(view, ShadowAtlasTile{ replacement });
        LOG_INFO("[ShadowViewSystem] Light {} moved from level {} to {}.",
            static_cast<uint32_t>(light), currentLevel,
            ShadowAtlasAllocator::Decode(replacement).m_level);
        return true;
    }

    void ShadowViewSystem::Activate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light,
        uint32_t level, const Math::Vector3& focus)
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
        // loop — see TODO_ShadowOptimizePlan.md §九 step 3.
        const RHI::RHIHandle viewHandle = refs->m_views[0];

        if (refs->m_baseIndex < 0)
        {
            const uint32_t tile = AllocateTileOrFiner(level);
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

            LOG_INFO("[ShadowViewSystem] Light {} took shadow tile {} at level {} (view row {}).",
                static_cast<uint32_t>(light), tile,
                ShadowAtlasAllocator::Decode(tile).m_level, row);
        }
        else if (!ReallocateTile(world, rhiCtx, viewHandle, light, level))
        {
            return;
        }

        View&          view    = rhiCtx.Get<View>(viewHandle);
        const uint32_t granted = GrantedLevel(rhiCtx, viewHandle);

        view.m_rect = ShadowTileRect(rhiCtx.Get<ShadowAtlasTile>(viewHandle).m_tile);
        if (rd->m_type == Light::LightType::Directional)
        {
            WriteDirectionalView(view, *rd, focus, granted);
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

        m_atlasAllocator.Reset();
        m_viewRows.reset();
        m_atlasFullLogged = false;

        // Strip the world-side refs so a re-init starts clean.
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<ShadowViewRefs>();
        }
    }

    uint32_t ShadowViewSystem::AllocateTile(uint32_t level)
    {
        const uint32_t node = m_atlasAllocator.Allocate(level);
        return node != ShadowAtlasAllocator::kInvalidNode ? node : kInvalidShadowSlot;
    }

    uint32_t ShadowViewSystem::AllocateTileOrFiner(uint32_t level)
    {
        // Down to the finest, so fragmentation is handled by the same rule as a tight budget:
        // the tree can hold a free 512 while no 1024 can be cut out of it, and a light in that
        // situation should get the 512 rather than nothing.
        for (uint32_t l = level; l <= kShadowFinestLevel; ++l)
        {
            const uint32_t node = m_atlasAllocator.Allocate(l);
            if (node != ShadowAtlasAllocator::kInvalidNode)
            {
                return node;
            }
        }

        if (!m_atlasFullLogged)
        {
            LOG_WARN("[ShadowViewSystem] The shadow atlas is full at every level down to {}; "
                     "further lights cast no shadow until space frees up.", kShadowFinestLevel);
            m_atlasFullLogged = true;
        }
        return kInvalidShadowSlot;
    }

    void ShadowViewSystem::ReleaseTile(uint32_t tile)
    {
        m_atlasAllocator.Free(tile);
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
