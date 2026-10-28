#pragma once

#include <ECS/WorldContext.h>
#include <Math/Sphere.h>
#include <Math/Vector3.h>
#include <RHI/Context/RHIContext.h>

#include "ShadowMapAllocator.h"
#include "ViewComponents.h"

namespace Spark::Render
{
    //! A view PRODUCER beside CameraViewSystem: reconciles ShadowViewTag view entities
    //! against the world's shadow-casting lights every frame — find-or-create per light,
    //! refresh its View, reap the orphans. Writes only the View component; encoding it into
    //! the view's g_Views row is ViewBindingSystem's job, which does that for every view regardless
    //! of who produced it.
    //!
    //! Decides which lights deserve a shadow and at what resolution; ShadowMapAllocator holds
    //! the shadow maps.
    //!
    //! Not an ISystem: a plain helper owned by RenderSystem and driven from
    //! RenderSystem::OnTick, sequenced before the encoding step.
    class ShadowViewSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void Update();
        //! After the GPU is idle.
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Commits the light's surviving shadow maps at the requested level, then refreshes
        //! their Views. The view entities are created on first activation and outlive any later
        //! deactivation.
        //! `volume` is the world region the light's shadows must cover, and only a
        //! directional light has one: every other type is bounded by its own range. Empty
        //! for those, and ignored.
        void Activate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light,
                      uint32_t level, uint32_t mapMask, const Math::Sphere& volume);

        //! Hands the shadow maps back and stops the views from rendering. ShadowMaps goes in the
        //! same call, which is what keeps the lighting shader from sampling a shadow map that now
        //! belongs to someone else.
        void Deactivate(WorldContext& world, RHI::RHIContext& rhiCtx, Entity light);

        //! Commits the shadow maps of mapMask at the level and decommits the others. The ones
        //! committed.
        uint32_t CommitMaps(ShadowMaps& maps, uint32_t level, uint32_t mapMask, uint32_t mapCount);

        //! Decommits every shadow map of the light and removes ShadowMaps. The views stay.
        void ReleaseMaps(WorldContext& world, Entity light, const ShadowViewRefs& refs);

        ShadowMapAllocator m_maps;

        //! Keeps the "out of shadow maps" warning to one line per episode: a light that finds none
        //! tries again every frame.
        bool m_outOfMapsLogged = false;
    };
}
