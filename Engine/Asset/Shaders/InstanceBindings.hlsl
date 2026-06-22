// Per-instance shader inputs (the "InstanceBindings" group), reserved at register
// space1. Shared engine header — any shader that needs per-instance data does:
//     #include <Shaders/InstanceBindings.hlsl>
// and the engine fills it via InstanceBindingSystem (Render::WriteInstanceConstants).
//
// space1 is the per-instance tier — one step above the per-view space0
// (ViewBindings). Per-material inputs must live in a HIGHER space than this.
#ifndef SPARK_INSTANCE_BINDINGS_HLSL
#define SPARK_INSTANCE_BINDINGS_HLSL

cbuffer InstanceBindings : register(b0, space1)
{
    float4x4 g_Model;   // object -> world
};

#endif // SPARK_INSTANCE_BINDINGS_HLSL
