#pragma once

#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    //! Producer-agnostic Drawable → DrawItem router. Each frame Process():
    //!  1. cascade-reaps Drawables whose referenced resources died (and the
    //!     DrawItems they routed out), then
    //!  2. asks every pass (PassCapabilities) which not-yet-derived Drawables it
    //!     accepts, building one DrawItem per accepting pass with everything the
    //!     submit path needs baked on — no back-reference to the Drawable — and
    //!     stamping it with that pass's PassTag, which is how the executer's
    //!     per-frame query later locates it.
    //!
    //! Deliberately does NOT know who produced a Drawable: world-composed
    //! (MeshDrawableComposer) or procedural (a feature processor) both flow through
    //! the same gate. Holds no internal state — the bridge is entirely in ECS
    //! components (dependency refs + DrawItemsDerivedTag on the Drawable, DerivedDrawItems
    //! as the reverse ref for teardown).
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked after
    //! MeshDrawableComposer and before the per-frame bind pass.
    class DrawItemRouter
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! Reap dead-dependency Drawables, then derive DrawItems for every
        //! not-yet-derived Drawable a pass accepts. Producer-agnostic.
        void Process();
        void Shutdown(RHI::RHIContext& rhiCtx);
    };
}
