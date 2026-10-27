#pragma once

#include <Pass/TagList.h>
#include <Suballocation/Suballocator.h>

#include "GeometryData.h"

namespace Spark::Render
{
    //! A MeshGeometry's g_Geometries slot, on the WORLD entity that holds it: a
    //! GeometryData-sized range of the array's address space. Get() is the GPU index, what
    //! InstanceData::m_geometryIndex stores.
    struct GeometrySlot
    {
        Suballocation m_range;

        uint32_t Get() const { return static_cast<uint32_t>(m_range.GetOffset() / sizeof(GeometryData)); }
    };

    //! The single shared ShaderBindings entity carrying g_Geometries (space6).
    struct GeometryBindingTag
    {
        using Uses = TagList<>;
    };

    //! The buffer entity g_Geometries is a view of, written in the graph. A pass whose shaders
    //! read g_Geometries imports it by this tag and declares the read in each Scope.
    struct GeometryPoolTag {};
}
