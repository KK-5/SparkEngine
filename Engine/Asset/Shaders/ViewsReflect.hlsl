// Reflection host for the shared space1 group holding g_Views, alongside the per-view
// ViewBindings SRG it is replacing. Never used to render — only ViewBindingSystem loads it,
// and only for reflection. See ViewBindingsReflect.hlsl for why a host exists at all.
//
// Temporary: when ViewBindings.hlsli itself declares g_Views, ViewBindingsReflect.hlsl
// takes over and this file goes away.
#include <Shaders/ViewData.hlsli>

StructuredBuffer<ViewData> g_Views : register(t0, space1);

float4 VSMain(float3 pos : POSITION) : SV_Position
{
    return mul(g_Views[0].viewProjection, float4(pos, 1.0));
}
