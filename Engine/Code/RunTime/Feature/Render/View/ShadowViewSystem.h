#pragma once

#include <EASTL/bitset.h>

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
    //! The atlas tile a view owns is allocated here and held for that view's whole lifetime.
    //! It has to be: LightData::m_shadowIndex addresses g_ShadowViews by it, so a slot that
    //! shifted when the light set changed would point one light's shadow at another light's
    //! tile for a frame.
    //!
    //! Not an ISystem: a plain helper owned by RenderSystem and driven from
    //! RenderSystem::OnTick, sequenced before the encoding step.
    class ShadowViewSystem
    {
    public:
        void Update();
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        uint32_t AllocateSlot();
        void     ReleaseSlot(uint32_t slot);

        //! Occupied tiles. Outlives the frame, unlike everything else about a shadow view.
        eastl::bitset<kShadowTileCount> m_slots;

        //! Keeps the "atlas full" warning to one line per episode: allocation is retried
        //! every frame, so a light that does not fit would otherwise log forever.
        bool m_atlasFullLogged = false;
    };
}
