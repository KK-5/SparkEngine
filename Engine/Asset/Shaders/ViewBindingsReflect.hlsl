// Reflection host for the ViewBindings group.
//
// ViewBindings.hlsl is a pure cbuffer header with no entry point, and the asset
// builder only compiles shaders whose source defines a known entry point — so it
// cannot compile/reflect ViewBindings.hlsl on its own. This file gives the group a
// dummy vertex entry that uses g_ViewProjection (so it survives optimization),
// purely so the engine can compile + reflect the ViewBindings layout. It is NEVER
// used to render — only ViewBindingSystem loads it, and only for reflection.
//
// NOTE: the stage detector is a plain substring scan of the source, so this comment
// deliberately avoids spelling out the other entry-point names — mentioning them
// here would make the builder try to compile stages that don't exist.
//
// Temporary: once the builder gains a real reflection-only path, this host shader
// goes away.
#include <Shaders/ViewBindings.hlsl>

float4 VSMain(float3 pos : POSITION) : SV_Position
{
    // Reference every member so none is optimized out of the reflected cbuffer.
    float4 clip  = mul(g_ViewProjection, float4(pos, 1.0));
    float4 world = mul(g_InvViewProj, clip);
    float4 vpos  = mul(g_View, float4(pos, 1.0));
    float4 wpos  = mul(g_InvView, vpos);
    return clip + (world + vpos + wpos) * 1e-6;
}
