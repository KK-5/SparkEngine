#pragma once

#include <RHI/Resource/AccessFlags.h>

#include <Pass/TagList.h>
#include <Suballocation/Suballocator.h>

#include "InstanceData.h"

namespace Spark::Render
{
    //! A renderable's g_Instances slot, on the WORLD entity: an InstanceData-sized range of
    //! the array's address space, which does not move for the renderable's life. Going, it is
    //! handed back once the GPU is done with it.
    struct InstanceSlot
    {
        Suballocation m_range;

        uint32_t Get() const { return static_cast<uint32_t>(m_range.GetOffset() / sizeof(InstanceData)); }
    };

    //! The model matrix a renderable was encoded with last frame, on the WORLD entity.
    //! Absent means no history: the previous transform is taken as the current one.
    struct InstanceHistory
    {
        Math::Matrix4X4 m_model{Math::Matrix4X4Const::IDENTITY};
    };

    //! The single shared ShaderBindings entity carrying g_Instances (space4).
    struct InstanceBindingTag
    {
        using Uses = TagList<>;
    };

    //! On an entity of its own, kept by InstanceBindingSystem: how many slots of g_Instances
    //! a reader of the whole array has to go through this frame. The high-water mark, not
    //! the number of live instances: the holes below it are counted.
    struct InstanceSlotCount
    {
        uint32_t m_count = 0;
    };

    //! The single shared per-instance vertex stream ID buffer entity, which a pass drawing
    //! meshes imports.
    struct InstanceIDBufferTag {};
}
