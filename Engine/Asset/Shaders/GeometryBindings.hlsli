// Per-geometry shader inputs (the "GeometryBindings" group), reserved at register
// space6. Shared engine header — a shader that turns instances into draws does:
//     #include <Shaders/GeometryBindings.hlsli>
// and the engine fills g_Geometries via GeometryBindingSystem (one element per mesh on
// the GPU). InstanceData::GeometryIndex indexes it.
//
// The spaces of all binding tiers are listed in Render/Binding/BindingSpaces.h.
#ifndef SPARK_GEOMETRY_BINDINGS_HLSL
#define SPARK_GEOMETRY_BINDINGS_HLSL

#include "GeometryData.hlsli"

// The group's layout is declared again in GeometryBindingSystem::Init; change both together.
StructuredBuffer<GeometryData> g_Geometries : register(t0, space6);

#endif // SPARK_GEOMETRY_BINDINGS_HLSL
