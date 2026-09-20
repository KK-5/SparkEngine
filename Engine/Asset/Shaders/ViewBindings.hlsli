// Per-view shader inputs (the "ViewBindings" group), reserved at register
// space1. Shared engine header — any shader that needs camera/view data does:
//     #include <Shaders/ViewBindings.hlsli>
// and the engine fills it in ViewBindingSystem, by member name.
//
// Binding-space convention (by update frequency, low = stable): space0 per-scene,
// space1 per-view, space2 per-pass, space3 per-material, space4 per-object.
#ifndef SPARK_VIEW_BINDINGS_HLSL
#define SPARK_VIEW_BINDINGS_HLSL

cbuffer ViewBindings : register(b0, space1)
{
    float4x4 g_ViewProjection;       // world -> clip, jittered: what rasterizes
    float4x4 g_InvViewProj;          // clip -> world, inverse of the jittered matrix
    float4x4 g_View;                 // world -> view
    float4x4 g_InvView;              // view  -> world (mul by view origin -> camera world pos)
    float4x4 g_ViewProjectionNoAA;   // world -> clip, unjittered
    float4x4 g_PrevViewProjection;   // previous frame's world -> clip, unjittered
    float4x4 g_ClipToPrevClip;       // unjittered clip -> previous frame's unjittered clip

    float4   g_TemporalAAJitter;     // NDC offsets: xy this frame, zw previous frame
    float4   g_ViewRectMin;          // xy: view rect origin in pixels of the buffer
    float4   g_InputViewRectMin;     // xy: origin of the region read from input images, in their pixels
    float4   g_ViewSizeAndInvSize;   // view rect in pixels: w, h, 1/w, 1/h
    float4   g_BufferSizeAndInvSize; // target the rect is part of: w, h, 1/w, 1/h
    float4   g_InvDeviceZToViewZ;    // see ConvertFromDeviceZ

    float    g_Exposure;             // linear pre-tonemap exposure multiplier; 1.0 = neutral

    // Encoding scale, not an artistic one: every shader that writes SceneColor multiplies
    // by it and Tonemap divides it back out, to sit the scene's magnitudes in a good part
    // of FP16's range. Fixed at 1 until EyeAdaptation drives it.
    float    g_PreExposure;
    float    g_OneOverPreExposure;

    uint     g_FrameNumber;
    float    g_GameTime;             // seconds; stops under pause, stretches under time scale
    float    g_PrevGameTime;
    float    g_DeltaTime;            // game time step
};

//! Device depth -> view-space depth, for the reversed-Z perspective and orthographic
//! projections Math::PerspectiveFov / OrthographicProjection build.
float ConvertFromDeviceZ(float deviceZ)
{
    return g_InvDeviceZToViewZ.z > 0.5
        ? g_InvDeviceZToViewZ.y / (deviceZ - g_InvDeviceZToViewZ.x)
        : (deviceZ - g_InvDeviceZToViewZ.y) / g_InvDeviceZToViewZ.x;
}

#endif // SPARK_VIEW_BINDINGS_HLSL
