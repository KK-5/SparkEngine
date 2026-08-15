#pragma once

#include <EASTL/bitset.h>

#include <ECS/WorldContext.h>
#include <Math/Vector3.h>
#include <RHI/Context/RHIContext.h>

#include "ShadowAtlasLayout.h"
#include "ViewComponents.h"

namespace Spark::Render
{
    //! A view PRODUCER beside CameraViewSystem: reconciles ShadowViewTag view entities
    //! against the world's shadow-casting lights every frame — find-or-create per light,
    //! refresh its View, reap the orphans. Writes only the View component; encoding it into
    //! the view's SRG is ViewBindingSystem's job, which does that for every view regardless
    //! of who produced it.
    //!
    //! Owns the atlas image too — a tile index means nothing without the atlas it indexes.
    //! ShadowPass finds it by ShadowAtlasTag.
    //!
    //! Two allocators, deliberately separate. An atlas tile is space to rasterize into; a
    //! g_ShadowViews row is where the matrices land. They are one number today only because
    //! both are first-fit over equally sized bitsets and are taken and returned together —
    //! nothing may rely on that, since a resolution ladder frees the tile side to become
    //! variable-size while rows stay dense.
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
        void Activate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light,
                      uint32_t level, uint32_t faceMask, const Math::Vector3& focus);

        //! Hands the tile and the row back and stops the view from rendering.
        //! ShadowViewRefs::m_baseIndex goes to -1 in the same call, which is what keeps the
        //! lighting shader from sampling a tile that now belongs to someone else.
        void Deactivate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light);

        //! The level of the tiles a light holds, or kNoLevel. The level is not stored: it is
        //! recovered from a tile id, which is what keeps the resolution hysteresis free of
        //! any state carried between frames.
        static uint32_t GrantedLevel(RHI::RHIContext& rhiCtx, const ShadowViewRefs& refs);

        //! Bit per face currently holding a tile.
        static uint32_t HeldFaceMask(RHI::RHIContext& rhiCtx, const ShadowViewRefs& refs);

        void ReleaseHeldTiles(RHI::RHIContext& rhiCtx, const ShadowViewRefs& refs);

        //! Brings the light's tiles in line with a level and a face set, all of them or none.
        //! Promoting and demoting are not symmetric — see the definition. False only when the
        //! light was left holding nothing and has been deactivated; a promotion that could not
        //! be afforded keeps the current tiles and returns true.
        bool ReallocateTiles(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light,
                             const ShadowViewRefs& refs, uint32_t level, uint32_t faceMask);

        //! count tiles at exactly that level, or none — a partial set is rolled back. For
        //! promotion, where keeping the smaller tiles the light already has beats releasing
        //! them for an allocation that may fail.
        bool AllocateTilesAt(uint32_t level, uint32_t count, uint32_t* outTiles);

        //! That level, or the coarsest finer one that fits all count of them. Returns the
        //! level granted, or kNoLevel. A light that cannot have the size it asked for takes
        //! smaller tiles rather than none, which is also what keeps one light's allocation
        //! from depending on another light's score.
        uint32_t AllocateTilesOrFiner(uint32_t level, uint32_t count, uint32_t* outTiles);

        void     ReleaseTile(uint32_t tile);

        //! Consecutive rows, so one light's faces are addressable from a single base index.
        //! Returns the first, or kInvalidShadowSlot.
        uint32_t AllocateViewRows(uint32_t count);
        void     ReleaseViewRows(uint32_t base, uint32_t count);

        //! Written by ShadowPass, read by LightingPass. Persistent, and deferred-init.
        RHI::RHIHandle m_atlas = RHI::NullHandle;

        //! Occupied tiles and rows. Outlive the frame, unlike everything else about a shadow
        //! view. Rows stay a flat bitset — they are all one size and always will be, which is
        //! the whole reason they are no longer the same number as a tile.
        ShadowAtlasAllocator               m_atlasAllocator;
        eastl::bitset<kShadowViewCapacity> m_viewRows;

        //! Keeps the "atlas full" warning to one line per episode: allocation is retried
        //! every frame, so a light that does not fit would otherwise log forever.
        bool m_atlasFullLogged = false;
    };
}
