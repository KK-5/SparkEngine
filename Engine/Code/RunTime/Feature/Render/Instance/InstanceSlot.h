#pragma once

#include <cstdint>

#include <EASTL/vector.h>

namespace Spark::Render
{
    //! Stable per-renderable id, allocated by InstanceBindingSystem on first
    //! renderable, freed on DeadTag. The current-frame g_Instances slot lives
    //! in InstanceSlotTable.m_slots[m_id] — indirection lets the table reorder
    //! slots every frame to match dense ECS storage (memcpy upload path)
    //! without disturbing any Drawable.
    struct InstanceSlotRef
    {
        uint32_t m_id = UINT32_MAX;
    };

    //! Component on the InstanceBindingTag entity. m_slots[id] = id's current
    //! slot in g_Instances; UINT32_MAX means freed (cascade-reap signal).
    struct InstanceSlotTable
    {
        eastl::vector<uint32_t> m_slots;
    };

    //! The single shared ShaderBindings entity carrying g_Instances (space1).
    struct InstanceBindingTag {};

    //! The single shared per-instance vertex stream ID buffer entity.
    struct InstanceIDBufferTag {};
}
