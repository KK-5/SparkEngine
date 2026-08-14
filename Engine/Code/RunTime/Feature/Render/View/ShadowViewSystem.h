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
    //! The tile a view owns is allocated here and held for that view's whole lifetime. It has
    //! to be: LightData::m_shadowIndex addresses g_ShadowViews by it, so a slot that shifted
    //! when the light set changed would point one light's shadow at another light's tile.
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

        //! Hands the tile back and stops the view from rendering. ShadowViewRefs::m_index
        //! goes to -1 in the same call, which is what keeps the lighting shader from
        //! sampling a tile that now belongs to someone else.
        void Deactivate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light);

        uint32_t AllocateSlot();
        void     ReleaseSlot(uint32_t slot);

        //! Written by ShadowPass, read by LightingPass. Persistent, and deferred-init.
        RHI::RHIHandle m_atlas = RHI::NullHandle;

        //! Occupied tiles. Outlives the frame, unlike everything else about a shadow view.
        eastl::bitset<kShadowTileCount> m_slots;

        //! Keeps the "atlas full" warning to one line per episode: allocation is retried
        //! every frame, so a light that does not fit would otherwise log forever.
        bool m_atlasFullLogged = false;
    };
}
