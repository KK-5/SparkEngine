// Per-view shader inputs (the "ViewBindings" group), reserved at register
// space1. Shared engine header — any shader that needs camera/view data does:
//     #include <Shaders/ViewBindings.hlsl>
// and the engine fills it via Render::WriteViewConstants(view, bindings).
//
// Binding-space convention (by update frequency, low = stable): space0 per-scene,
// space1 per-view, space2 per-pass, space3 per-material, space4 per-object.
#ifndef SPARK_VIEW_BINDINGS_HLSL
#define SPARK_VIEW_BINDINGS_HLSL

cbuffer ViewBindings : register(b0, space1)
{
    float4x4 g_ViewProjection;   // world -> clip (projection * view)
    float4x4 g_InvViewProj;      // clip  -> world (inverse of g_ViewProjection)
    float4x4 g_View;             // world -> view
    float4x4 g_InvView;          // view  -> world (mul by view origin -> camera world pos)
    float    g_Exposure;         // linear pre-tonemap exposure multiplier; 1.0 = neutral
};

#endif // SPARK_VIEW_BINDINGS_HLSL
