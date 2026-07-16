// Per-material shader inputs (the "MaterialBindings" group), reserved at register
// space2 — the per-material tier, one step above the per-instance space1
// (InstanceBindings). Shared engine header — any shader that needs per-material data:
//     #include <Shaders/MaterialBindings.hlsl>
// and the engine fills g_Materials via MaterialBindingSystem (one element per material
// entity, per frame).
//
// The per-draw material index is delivered through InstanceData (InstanceData.MaterialIndex),
// resolved per instance by InstanceBindingSystem — NOT a per-material vertex stream.
#ifndef SPARK_MATERIAL_BINDINGS_HLSL
#define SPARK_MATERIAL_BINDINGS_HLSL

#include "MaterialData.hlsli"

StructuredBuffer<MaterialData> g_Materials : register(t0, space2);

// Abstraction seam: today materialIdx indexes g_Materials directly. If a future path
// changes how the real record is found, only this body changes — passes stay untouched.
MaterialData GetMaterialData(uint materialIdx)
{
    return g_Materials[materialIdx];
}

#endif // SPARK_MATERIAL_BINDINGS_HLSL
