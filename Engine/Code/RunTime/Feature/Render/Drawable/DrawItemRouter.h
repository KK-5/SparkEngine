#pragma once

#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    //! Producer-agnostic GeometrySpec → DrawItem router. Each frame Process():
    //!  1. cascade-reaps specs whose referenced resources died, then
    //!  2. resolves each not-yet-derived spec into ONE RHI::DrawItem on the same entity,
    //!     then stamps that entity with the PassTag of every pass (PassCapabilities) that
    //!     accepts it — which is how the executer's per-frame query later locates it.
    //!
    //! GeometrySpec and DrawItem are the unresolved and resolved forms of one object on
    //! one entity, so there is no link to maintain in either direction: DrawItem present
    //! ⟺ derived, and one DeadTag reaps both.
    //!
    //! Deliberately does NOT know who produced a spec: world-composed
    //! (MeshGeometryComposer) or procedural (a feature processor) both flow through
    //! the same gate. Holds no internal state.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked after
    //! MeshGeometryComposer and before the per-frame bind pass.
    class DrawItemRouter
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! Reap dead-dependency specs, then derive DrawItems for every
        //! not-yet-derived spec a pass accepts. Producer-agnostic.
        void Process();
        void Shutdown(RHI::RHIContext& rhiCtx);
    };
}
