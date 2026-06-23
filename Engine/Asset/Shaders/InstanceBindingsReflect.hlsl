// Reflection host for the InstanceBindings group.
//
// InstanceBindings.hlsl declares a StructuredBuffer with no entry point, and the
// asset builder only compiles shaders that define a known entry point — so it
// cannot reflect InstanceBindings.hlsl on its own. This file gives the group a
// dummy vertex entry that reads g_Instances (so the binding survives
// optimization), purely so the engine can reflect the space1 layout. It is NEVER
// used to render — only InstanceBindingSystem loads it, and only for reflection.
//
// NOTE: the stage detector is a plain substring scan of the source, so this file
// deliberately defines only a vertex entry and avoids spelling out other
// entry-point names — mentioning them would make the builder try to compile
// stages that don't exist.
//
// Temporary: once the builder gains a real reflection-only path, this host shader
// goes away.
#include <Shaders/InstanceBindings.hlsl>

float4 VSMain(uint instanceIdx : INSTANCE_INDEX) : SV_Position
{
    InstanceData inst = GetInstanceData(instanceIdx);
    return mul(inst.Model, float4(0.0, 0.0, 0.0, 1.0));
}
