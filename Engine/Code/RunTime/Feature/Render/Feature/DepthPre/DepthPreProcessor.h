#pragma once

#include <Math/Vector2.h>
#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    class PassContext;

    //! Builds the DepthPrePass draw list each frame. Stateless across frames: it
    //! emits transient DrawRequests for the entities renderable THIS frame (those
    //! carrying an InstanceSlot from InstanceBindingSystem) and reaps the previous
    //! frame's DrawRequests at the start of Process. The per-view (space0) and
    //! per-instance (space1, g_Instances) bindings plus the per-instance ID vertex
    //! stream are shared resources it only references — see InstanceBindingSystem
    //! and TODO_InstanceBindingSystemPlan.md §6.
    class DepthPreProcessor final
    {
    public:
        void Init(PassContext& passCtx, RHI::RHIContext& rhiCtx);
        void Shutdown(PassContext& passCtx);
        void Process(const Math::Vector2Int& renderSize);
    };
}
