// Shared coordinate and geometry checks. Consumers own textures, filtering and
// signal-specific history policy; no per-pass inputs are declared here.
#ifndef SPARK_LIB_TEMPORAL_REPROJECTION_HLSLI
#define SPARK_LIB_TEMPORAL_REPROJECTION_HLSLI

#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Lib/Velocity.hlsli>

float2 TemporalNDCToUVDelta(float2 ndcDelta)
{
    return ndcDelta * float2(0.5, -0.5);
}

float2 TemporalBufferPixelToViewUV(ViewData view, float2 pixelPosition)
{
    return (pixelPosition - view.viewRectMin.xy) * view.viewSizeAndInvSize.zw;
}

float2 TemporalViewUVToBufferUV(ViewData view, float2 viewUV)
{
    return (view.viewRectMin.xy + viewUV * view.viewSizeAndInvSize.xy) * view.bufferSizeAndInvSize.zw;
}

bool IsTemporalViewUVValid(float2 viewUV)
{
    return all(viewUV >= 0.0) && all(viewUV < 1.0);
}

bool IsTemporalMotionValid(float2 velocity)
{
    // CalcVelocity uses kVelocityMax for invalid homogeneous projections.
    return all(abs(velocity) < kVelocityMax);
}

// Both inputs/outputs are view-local, Y-down UVs. The output grid is unjittered;
// its colour-history coordinate differs from the raster sample's by current jitter.
float2 ReprojectTemporalHistoryUV(float2 outputViewUV, float2 velocity)
{
    return outputViewUV - TemporalNDCToUVDelta(velocity);
}

float2 ReprojectTemporalDepthUV(ViewData view, float2 rasterViewUV, float2 velocity)
{
    const float2 sampleViewUV = rasterViewUV - TemporalNDCToUVDelta(view.temporalAAJitter.xy);
    return ReprojectTemporalHistoryUV(sampleViewUV, velocity)
         + TemporalNDCToUVDelta(view.temporalAAJitter.zw);
}

struct TemporalSurfaceProjection
{
    float2 ViewUV; // previous, unjittered
    float ViewZ;   // positive depth in the previous camera
    bool Valid;
};

// Supply the previous corresponding world position. Reusing the current world
// point here is valid only for static surfaces; moving instances need PrevModel
// or the depth motion produced by CalcSurfaceVelocity.
TemporalSurfaceProjection ProjectTemporalSurface(ViewData view, float3 previousWorldPosition)
{
    TemporalSurfaceProjection result = (TemporalSurfaceProjection)0;
    const float4 clipPosition = mul(view.prevViewProjection, float4(previousWorldPosition, 1.0));
    const float viewZ = mul(view.prevView, float4(previousWorldPosition, 1.0)).z;
    if (!all(isfinite(clipPosition)) || !isfinite(viewZ) || clipPosition.w <= 0.0 || viewZ <= 0.0)
    {
        return result;
    }
    const float3 ndc = clipPosition.xyz / clipPosition.w;
    result.ViewUV = ndc.xy * float2(0.5, -0.5) + 0.5;
    result.ViewZ = viewZ;
    result.Valid = IsTemporalViewUVValid(result.ViewUV) && ndc.z > 0.0 && ndc.z <= 1.0;
    return result;
}

bool IsTemporalDepthConsistent(float expectedViewZ, float previousDeviceZ,
                               float4 previousDepthParameters, float absoluteTolerance, float relativeTolerance)
{
    // Zero is background in the engine's reversed-Z buffers, not a finite surface.
    if (!isfinite(expectedViewZ) || expectedViewZ <= 0.0 || !isfinite(previousDeviceZ)
        || previousDeviceZ <= 0.0 || previousDeviceZ > 1.0)
    {
        return false;
    }
    const float actualViewZ = ConvertFromDeviceZ(previousDepthParameters, previousDeviceZ);
    const float tolerance = max(absoluteTolerance, 0.0) + max(relativeTolerance, 0.0) * expectedViewZ;
    return isfinite(actualViewZ) && actualViewZ > 0.0 && abs(actualViewZ - expectedViewZ) <= tolerance;
}

#endif // SPARK_LIB_TEMPORAL_REPROJECTION_HLSLI
