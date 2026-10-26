#pragma once

#include <RHI/Resource/AccessFlags.h>

#include <Pass/TagList.h>
#include <Suballocation/Suballocator.h>

#include "InstanceData.h"

namespace Spark::Render
{
    //! How many instances there can be: slots of g_Instances, and entries of the instance IDs.
    constexpr uint32_t InstanceCapacity = 65536;

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

    //! The buffer entity g_Instances is a view of, written in the graph. A pass whose shaders
    //! read g_Instances imports it by this tag and declares the read in each Scope.
    struct InstancePoolTag {};

    //! On an entity of its own, kept by InstanceBindingSystem: how many entries of the instance
    //! IDs are this frame's, one for each renderable holding a slot. A reader of every instance
    //! goes through them.
    struct ActiveInstanceCount
    {
        uint32_t m_count = 0;
    };

    //! The single shared instance ID buffer entity: the active list, position -> g_Instances
    //! slot, packed and in no fixed order. A pass drawing meshes imports it as the per-instance
    //! vertex stream, and a draw's FirstInstance is a position in it; one going through every
    //! instance reads it as g_InstanceIDs.
    struct InstanceIDBufferTag {};
}
