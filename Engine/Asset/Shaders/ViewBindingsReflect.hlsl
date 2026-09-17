// Reflection host for the ViewBindings group.
//
// ViewBindings.hlsli is a pure cbuffer header with no entry point, and the asset
// builder only compiles shaders whose source defines a known entry point — so it
// cannot compile/reflect ViewBindings.hlsli on its own. This file gives the group a
// dummy vertex entry that uses g_ViewProjection (so it survives optimization),
// purely so the engine can compile + reflect the ViewBindings layout. It is NEVER
// used to render — only ViewFactory loads it, and only for reflection.
//
// NOTE: the stage detector is a plain substring scan of the source, so this comment
// deliberately avoids spelling out the other entry-point names — mentioning them
// here would make the builder try to compile stages that don't exist.
//
// Temporary: once the builder gains a real reflection-only path, this host shader
// goes away.
#include <Shaders/ViewBindings.hlsli>

float4 VSMain(float3 pos : POSITION) : SV_Position
{
    // Reference every member so none is optimized out of the reflected cbuffer.
    float4 clip  = mul(g_ViewProjection, float4(pos, 1.0));
    float4 world = mul(g_InvViewProj, clip);
    float4 vpos  = mul(g_View, float4(pos, 1.0));
    float4 wpos  = mul(g_InvView, vpos);
    float4 noAA  = mul(g_ViewProjectionNoAA, float4(pos, 1.0));
    float4 prev  = mul(g_PrevViewProjection, float4(pos, 1.0));
    float4 c2p   = mul(g_ClipToPrevClip, clip);
    float4 v4    = g_TemporalAAJitter + g_ViewRectMin + g_ViewSizeAndInvSize + g_BufferSizeAndInvSize + g_InvDeviceZToViewZ;
    float  s     = g_Exposure + (float)g_FrameNumber + g_GameTime + g_PrevGameTime + g_DeltaTime;
    return clip + (world + vpos + wpos + noAA + prev + c2p + v4) * 1e-6 + s * 1e-6;
}
