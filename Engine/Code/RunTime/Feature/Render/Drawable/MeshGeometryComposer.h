#pragma once

#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    //! World → GeometrySpec producer. Each frame Update() does find-or-create over
    //! renderable world entities (Mesh geometry + instance slot) not yet tagged
    //! with WorldComposedTag, emitting one GeometrySpec per entity. This is ONE
    //! spec producer among several (procedural passes compose their own);
    //! it is deliberately NOT responsible for reaping or for DrawItem derivation —
    //! those are producer-agnostic and live in DrawItemRouter, so any spec,
    //! whoever produced it, routes to the passes the same way.
    //!
    //! Holds no internal state — the world→spec bridge is entirely in ECS
    //! components (WorldComposedTag on the world entity, dependency refs on the
    //! GeometrySpec).
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked between
    //! InstanceBindingSystem and DrawItemRouter.
    class MeshGeometryComposer
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! World → GeometrySpec: find-or-create over renderable world entities. Does NOT
        //! reap and does NOT derive DrawItems.
        void Update();
        void Shutdown(RHI::RHIContext& rhiCtx);
    };
}
