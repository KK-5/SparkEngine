#pragma once

#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    //! Bridges world entities and Drawable entities. Each frame Update() cascade-reaps
    //! Drawables whose referenced resources died, then composes new Drawables for
    //! renderable world entities not yet tagged with WorldComposedTag. DrawItem
    //! derivation is a SEPARATE, producer-agnostic step (DeriveDrawItems) so any
    //! Drawable — world-composed or created elsewhere — routes through the passes the
    //! same way. Holds no internal state — the bridge is entirely in ECS components
    //! (WorldComposedTag on world, dependency refs + DrawItemsDerivedTag on Drawable).
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked between
    //! InstanceBindingSystem and the Processors.
    class DrawableComposer
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! World → Drawable: cascade reap + find-or-create. Does NOT derive DrawItems.
        void Update();
        //! Drawable → DrawItems: over every DrawableTag Drawable not yet derived (and
        //! whose geometry/binding deps have materialized), build one DrawItem per
        //! accepting pass route. Producer-agnostic; call after Update(), before the
        //! per-frame bind pass.
        void DeriveDrawItems();
        void Shutdown(RHI::RHIContext& rhiCtx);
    };
}
