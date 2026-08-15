#pragma once

#include <EASTL/bitset.h>

#include <ECS/WorldContext.h>
#include <Math/Vector3.h>
#include <RHI/Context/RHIContext.h>

#include "ShadowAtlasLayout.h"

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
        //! Gives the light a tile at the requested level if it lacks one or is holding the
        //! wrong size, then refreshes its View. The view entity is created on first
        //! activation and outlives any later deactivation.
        void Activate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light,
                      uint32_t level, const Math::Vector3& focus);

        //! Hands the tile and the row back and stops the view from rendering.
        //! ShadowViewRefs::m_baseIndex goes to -1 in the same call, which is what keeps the
        //! lighting shader from sampling a tile that now belongs to someone else.
        void Deactivate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light);

        //! The level of the tile a view holds, or kNoLevel. The level is not stored: it is
        //! recovered from the tile id, which is what keeps the resolution hysteresis free of
        //! any state carried between frames.
        static uint32_t GrantedLevel(RHI::RHIContext& rhiCtx, RHI::RHIHandle view);

        //! Exchanges the tile a view holds for one of a different level, keeping its
        //! g_ShadowViews row. Growing and shrinking are not symmetric — see the definition.
        //! False only when the view was left holding nothing and has been deactivated; a
        //! promotion that could not be afforded keeps the current tile and returns true.
        bool ReallocateTile(WorldContext& world, RHI::RHIContext& rhiCtx, RHI::RHIHandle view,
                            Entity light, uint32_t level);

        //! That level or nothing. For promotion, where keeping the smaller tile the light
        //! already has beats releasing it for an allocation that may fail.
        uint32_t AllocateTile(uint32_t level);

        //! That level, or the coarsest finer one available. A light that cannot have the size
        //! it asked for takes a smaller tile rather than nothing, which is also what keeps one
        //! light's allocation from depending on another light's score.
        uint32_t AllocateTileOrFiner(uint32_t level);

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
