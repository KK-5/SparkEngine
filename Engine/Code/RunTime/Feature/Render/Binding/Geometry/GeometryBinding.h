#pragma once

#include <Binding/GlobalBuffer.h>

#include "GeometryData.h"

namespace Spark::Render
{
    //! Names the g_Geometries array (space6) for GlobalBuffer and its slot refs.
    struct Geometries {};

    //! A MeshGeometry's g_Geometries slot, on the WORLD entity that holds it. Get() is the
    //! GPU index, what InstanceData::m_geometryIndex stores.
    using GeometrySlotRef = SlotRef<Geometries>;

    //! The single shared ShaderBindings entity carrying g_Geometries (space6).
    struct GeometryBindingTag {};
}
