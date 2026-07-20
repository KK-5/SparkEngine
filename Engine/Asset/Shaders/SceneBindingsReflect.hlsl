// Reflection host for the SceneBindings group.
//
// SceneBindings.hlsl is a pure resource/cbuffer header with no entry point, so the asset
// builder cannot compile/reflect it alone. This file gives the group a dummy vertex entry
// that references g_Lights AND g_LightCount (so neither is optimized out of the reflected
// layout), purely so SceneBindingSystem can compile + reflect the space0 layout. It is
// NEVER used to render — only SceneBindingSystem loads it, and only for reflection.
//
// NOTE: the stage detector is a plain substring scan of the source, so this comment
// deliberately avoids spelling out the other entry-point names.
#include <Shaders/SceneBindings.hlsl>

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position
{
    LightData l = GetLight(0);
    float w = (float)g_LightCount;
    return float4(l.direction + l.color + l.position, l.intensity + w) * 1e-6;
}
