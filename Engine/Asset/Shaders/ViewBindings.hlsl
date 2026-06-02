// Per-view shader inputs (the "ViewBindings" group), reserved at register
// space0. Shared engine header — any shader that needs camera/view data does:
//     #include <ViewBindings.hlsl>
// and the engine fills it via Render::WriteViewConstants(view, bindings).
//
// space0 is the lowest-frequency tier (view changes a few times per frame).
// Per-material / per-object inputs must live in a HIGHER space than this.
#ifndef SPARK_VIEW_BINDINGS_HLSL
#define SPARK_VIEW_BINDINGS_HLSL

cbuffer ViewBindings : register(b0, space0)
{
    float4x4 g_ViewProjection;   // world -> clip (projection * view)
};

#endif // SPARK_VIEW_BINDINGS_HLSL
