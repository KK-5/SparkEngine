// Per-geometry shader inputs (the "GeometryBindings" group), reserved at register
// space6. Shared engine header — a shader that turns instances into draws does:
//     #include <Shaders/GeometryBindings.hlsli>
// and the engine fills g_Geometries via GeometryBindingSystem (one element per mesh on
// the GPU). InstanceData::GeometryIndex indexes it.
//
// It extends the convention (space0 per-scene, space1 per-view, space2 per-pass,
// space3 per-material, space4 per-object, space5 per-Scope) with space6 per-geometry.
#ifndef SPARK_GEOMETRY_BINDINGS_HLSL
#define SPARK_GEOMETRY_BINDINGS_HLSL

#include "GeometryData.hlsli"

StructuredBuffer<GeometryData> g_Geometries : register(t0, space6);

#endif // SPARK_GEOMETRY_BINDINGS_HLSL
