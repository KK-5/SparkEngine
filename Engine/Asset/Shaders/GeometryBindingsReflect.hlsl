// Reflection host for the GeometryBindings group.
//
// GeometryBindings.hlsli declares a StructuredBuffer with no entry point, and the asset
// builder only compiles shaders that define a known entry point. This file gives the
// group a dummy vertex entry that reads g_Geometries (so the binding survives
// optimization), purely so the engine can reflect the space6 layout. It is NEVER used to
// render — only GeometryBindingSystem loads it, and only for reflection.
//
// NOTE: the stage detector is a plain substring scan of the source, so this file
// deliberately defines only a vertex entry and avoids spelling out other entry-point
// names — mentioning them would make the builder try to compile stages that don't exist.
#include <Shaders/GeometryBindings.hlsli>

float4 VSMain(uint geometryIdx : SV_VertexID) : SV_Position
{
    GeometryData geometry = g_Geometries[geometryIdx];
    return float4(geometry.FirstIndex, geometry.IndexCount, geometry.VertexOffset, 1.0);
}
