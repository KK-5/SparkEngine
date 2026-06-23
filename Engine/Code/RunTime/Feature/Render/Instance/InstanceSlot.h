#pragma once

#include <cstdint>

namespace Spark::Render
{
    //! Per-frame slot index into the global g_Instances StructuredBuffer, placed
    //! on a WORLD entity by InstanceBindingSystem::Update each frame.
    //!
    //! ⚠ STRICTLY per-frame — never cache across frames. InstanceBindingSystem is
    //! free to renumber every frame; consumers MUST re-query in their Process step.
    //! See TODO_InstanceBindingSystemPlan.md §11 (the one inviolable contract):
    //! existence ⟺ renderable this frame; the value is not stable across frames.
    struct InstanceSlot
    {
        uint32_t m_index = UINT32_MAX;
    };

    //! Identifies, in the RHIContext, the single shared ShaderBindings entity that
    //! carries the g_Instances StructuredBuffer SRV (space1).
    struct InstanceBindingTag {};

    //! Identifies, in the RHIContext, the single shared per-instance vertex stream
    //! ID buffer entity (content [0, 1, ..., Capacity-1]). See plan §2.5.
    struct InstanceIDBufferTag {};
}
