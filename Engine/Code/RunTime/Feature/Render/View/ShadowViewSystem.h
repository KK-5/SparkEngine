#pragma once

#include <ECS/WorldContext.h>
#include <Math/Sphere.h>
#include <Math/Vector3.h>
#include <RHI/Context/RHIContext.h>

#include "ShadowAtlasAllocator.h"
#include "ViewComponents.h"

namespace Spark::Render
{
    //! A view PRODUCER beside CameraViewSystem: reconciles ShadowViewTag view entities
    //! against the world's shadow-casting lights every frame — find-or-create per light,
    //! refresh its View, reap the orphans. Writes only the View component; encoding it into
    //! the view's SRG is ViewBindingSystem's job, which does that for every view regardless
    //! of who produced it.
    //!
    //! Decides which lights deserve atlas space and how much; ShadowAtlasAllocator says
    //! whether that space exists.
    //!
    //! Not an ISystem: a plain helper owned by RenderSystem and driven from
    //! RenderSystem::OnTick, sequenced before the encoding step.
    class ShadowViewSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void Update();
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Gives the light's surviving faces tiles at the requested level, then refreshes
        //! their Views. The view entities are created on first activation and outlive any
        //! later deactivation.
        //! `volume` is the world region the light's shadows must cover, and only a
        //! directional light has one: every other type is bounded by its own range. Empty
        //! for those, and ignored.
        void Activate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light,
                      uint32_t level, uint32_t faceMask, const Math::Sphere& volume);

        //! Hands the tile and the row back and stops the view from rendering.
        //! ShadowViewRefs::m_baseIndex goes to -1 in the same call, which is what keeps the
        //! lighting shader from sampling a tile that now belongs to someone else.
        void Deactivate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light);

        //! The level of the tiles a light holds, or kNoShadowLevel. The level is not stored:
        //! it is recovered from a tile id, which is what keeps the resolution hysteresis free
        //! of any state carried between frames.
        static uint32_t GrantedLevel(RHI::RHIContext& rhiCtx, const ShadowViewRefs& refs);

        //! Bit per face currently holding a tile.
        static uint32_t HeldFaceMask(RHI::RHIContext& rhiCtx, const ShadowViewRefs& refs);

        //! Tiles only. The rows outlive them on purpose, so m_shadowIndex survives a change of
        //! level or face set — the name says which half so a new caller has to notice.
        void ReleaseTilesKeepingRows(RHI::RHIContext& rhiCtx, const ShadowViewRefs& refs);

        //! Everything the light holds. The two callers differ only in what they then do to
        //! the view entities.
        void ReleaseAllocation(RHI::RHIContext& rhiCtx, const ShadowViewRefs& refs);

        //! Brings the light's tiles in line with a level and a face set, all of them or none.
        //! Promoting and demoting are not symmetric — see the definition. False only when the
        //! light was left holding nothing and has been deactivated; a promotion that could not
        //! be afforded keeps the current tiles and returns true.
        bool ReallocateTiles(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light,
                             const ShadowViewRefs& refs, uint32_t level, uint32_t faceMask);

        ShadowAtlasAllocator m_atlas;
    };
}
