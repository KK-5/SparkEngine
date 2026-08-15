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
        //! Gives the light a tile if it lacks one, then refreshes its View. The view entity
        //! is created on first activation and outlives any later deactivation.
        void Activate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light,
                      const Math::Vector3& focus);

        //! Hands the tile and the row back and stops the view from rendering.
        //! ShadowViewRefs::m_baseIndex goes to -1 in the same call, which is what keeps the
        //! lighting shader from sampling a tile that now belongs to someone else.
        void Deactivate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light);

        uint32_t AllocateTile();
        void     ReleaseTile(uint32_t tile);
        uint32_t AllocateViewIndex();
        void     ReleaseViewIndex(uint32_t index);

        //! Written by ShadowPass, read by LightingPass. Persistent, and deferred-init.
        RHI::RHIHandle m_atlas = RHI::NullHandle;

        //! Occupied tiles and rows. Outlive the frame, unlike everything else about a
        //! shadow view.
        eastl::bitset<kShadowTileCount>    m_tiles;
        eastl::bitset<kShadowViewCapacity> m_viewRows;

        //! Keeps the "atlas full" warning to one line per episode: allocation is retried
        //! every frame, so a light that does not fit would otherwise log forever.
        bool m_atlasFullLogged = false;
    };
}
