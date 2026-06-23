// Per-instance shader inputs (the "InstanceBindings" group), reserved at register
// space1. Shared engine header — any shader that needs per-instance data does:
//     #include <Shaders/InstanceBindings.hlsl>
// and the engine fills g_Instances via InstanceBindingSystem (one element per
// renderable entity, per frame).
//
// space1 is the per-instance tier — one step above the per-view space0
// (ViewBindings). Per-material inputs must live in a HIGHER space than this.
//
// The per-draw instance index is delivered through a per-instance vertex stream
// (PER_INSTANCE_DATA, semantic INSTANCE_INDEX) + DrawInstanced's
// StartInstanceLocation, NOT a constant — see TODO_InstanceBindingSystemPlan.md §2.5.
#ifndef SPARK_INSTANCE_BINDINGS_HLSL
#define SPARK_INSTANCE_BINDINGS_HLSL

#include "InstanceData.hlsli"

StructuredBuffer<InstanceData> g_Instances : register(t0, space1);

// Abstraction seam: today instanceIdx comes straight from the vertex input. If a
// future path (e.g. same-mesh batching with an indirection table) changes how the
// real slot is found, only this body changes — passes stay untouched.
InstanceData GetInstanceData(uint instanceIdx)
{
    return g_Instances[instanceIdx];
}

#endif // SPARK_INSTANCE_BINDINGS_HLSL
