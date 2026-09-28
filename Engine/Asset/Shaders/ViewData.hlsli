// One entry of the global g_Views StructuredBuffer (space1), indexed by the view's stable
// slot. C++ mirror lives in Render/Binding/View/ViewData.h — keep the two identical.
#ifndef SPARK_VIEW_DATA_HLSLI
#define SPARK_VIEW_DATA_HLSLI

// 592 bytes.
struct ViewData
{
    float4x4 viewProjection;            // world -> clip, jittered: what rasterizes
    float4x4 invViewProj;               // clip -> world, inverse of the jittered matrix
    float4x4 view;                      // world -> view
    float4x4 invView;                   // view  -> world (mul by view origin -> camera world pos)
    float4x4 viewProjectionNoAA;        // world -> clip, unjittered
    float4x4 prevViewProjection;        // previous frame's world -> clip, unjittered
    float4x4 clipToPrevClip;            // unjittered clip -> previous frame's unjittered clip

    float4   temporalAAJitter;          // NDC offsets: xy this frame, zw previous frame
    float4   viewRectMin;               // xy: view rect origin in pixels of the buffer
    float4   inputViewRectMin;          // xy: origin of the region read from input images, in their pixels
    float4   viewSizeAndInvSize;        // view rect in pixels: w, h, 1/w, 1/h
    float4   bufferSizeAndInvSize;      // target the rect is part of: w, h, 1/w, 1/h
    float4   inputBufferSizeAndInvSize; // input images the region is read from: w, h, 1/w, 1/h
    float4   invDeviceZToViewZ;         // see ConvertFromDeviceZ

    float    exposure;                  // linear pre-tonemap exposure multiplier; 1.0 = neutral
    // Encoding scale, not an artistic one: every shader that writes SceneColor multiplies
    // by it and Tonemap divides it back out, to sit the scene's magnitudes in a good part
    // of FP16's range. Fixed at 1 until EyeAdaptation drives it.
    float    preExposure;
    float    oneOverPreExposure;
    uint     frameNumber;

    float    gameTime;                  // seconds; stops under pause, stretches under time scale
    float    prevGameTime;
    float    deltaTime;                 // game time step
    uint     padding0;
};

#endif // SPARK_VIEW_DATA_HLSLI
