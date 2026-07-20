// Reflection host for the MaterialBindings group. Mirrors InstanceBindingsReflect.hlsl.
//
// MaterialBindings.hlsl declares a StructuredBuffer with no entry point, and the asset
// builder only compiles shaders that define a known entry point — so it cannot reflect
// MaterialBindings.hlsl on its own. This file gives the group a dummy vertex entry that
// reads g_Materials (so the binding survives optimization), purely so the engine can
// reflect the space3 layout. It is NEVER used to render — only MaterialBindingSystem
// loads it, and only for reflection.
//
// NOTE: the stage detector is a plain substring scan of the source, so this file
// deliberately defines only a vertex entry and avoids spelling out other entry-point
// names — mentioning them would make the builder try to compile stages that don't exist.
#include <Shaders/MaterialBindings.hlsl>

float4 VSMain(uint materialIdx : SV_VertexID) : SV_Position
{
    MaterialData mat = GetMaterialData(materialIdx);
    return mat.BaseColor;
}
