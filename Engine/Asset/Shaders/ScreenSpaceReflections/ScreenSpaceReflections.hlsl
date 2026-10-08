// ScreenSpaceReflections.hlsl — what each pixel's mirror direction sees on screen: one ray per
// pixel marched through the depth chain (Lib/ScreenTrace.hlsli), and where it lands, last
// frame's finished color, or this frame's unfinished one where there is no last. rgb is that
// radiance, a how far ReflectionsPass lets it replace the environment's reflection.
//
// One ray along the mirror direction, whatever the roughness: there is no noise to clean up,
// and a rough surface would get a reflection too sharp for it, so the result fades out towards
// g_MaxRoughness instead.

#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Lib/DeferredShadingCommon.hlsli>
#include <Shaders/Lib/Velocity.hlsli>
#include <Shaders/Lib/ScreenTrace.hlsli>

struct ScopeParameters
{
    uint viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

Texture2D<float>    g_Depth          : register(t0, space2);   // SceneDepth, viewed as R32_FLOAT
Texture2D<float>    g_HZBClosest     : register(t1, space2);   // all its mips
Texture2D<float4>   g_GBufferNormal  : register(t2, space2);
Texture2D<float4>   g_GBufferSurface : register(t3, space2);
Texture2D<float2>   g_Velocity       : register(t4, space2);   // ResolvedVelocity
Texture2D<float4>   g_History        : register(t5, space2);   // last frame's TemporalAA
Texture2D<float4>   g_SceneColor     : register(t6, space2);   // this frame's, as it stands: lit, without reflections or sky
RWTexture2D<float4> g_Output         : register(u0, space2);

SamplerState g_LinearSampler : register(s0, space2);

cbuffer ScreenSpaceReflectionsParams : register(b0, space2)
{
    float2 g_HZBUvFactor;       // screen UV times this is HZB UV
    uint2  g_HZBSize;           // of the HZB's mip 0
    uint   g_HZBMipCount;
    // 0 when there is no last frame: g_History's content is undefined then, and must be
    // selected away, never weighted.
    uint   g_HistoryValid;
    // From the view's ViewScreenSpaceReflection, see there.
    uint   g_StepCountMax;
    float  g_MaxRoughness;
    float  g_Intensity;
};

// The share of g_MaxRoughness, below it, over which the reflection fades out.
static const float kRoughnessFadeRange = 0.25;

// How far behind the surface it shows a ray may stop and still count as on it, as a share of
// that surface's distance from the eye: the depth a pixel spans grows with the distance.
static const float kThickness = 0.02;

// A ray coming back at the eye (the cosine between the two) sees the side of things the
// screen does not show.
static const float kTowardEyeFadeStart = 0.5;
static const float kTowardEyeFadeEnd   = 0.9;

float3 WorldPosition(ViewData view, float2 uv, float deviceZ)
{
    const float4 world = mul(view.invViewProj, float4(ScreenTrace_ScreenUVToNdc(uv), deviceZ, 1.0));
    return world.xyz / world.w;
}

float4 Reflection(uint2 px, ViewData view)
{
    const float2 size   = view.bufferSizeAndInvSize.xy;
    const float2 invSize = view.bufferSizeAndInvSize.zw;

    // Reversed-Z: the sky is at 0.
    const float depth = g_Depth.Load(int3(px, 0));
    if (depth <= 0.0)
    {
        return 0.0;
    }

    const float4 surface = g_GBufferSurface.Load(int3(px, 0));
    uint shadingModel;
    uint selectiveOutputMask;
    DecodeShadingModel(surface.a, shadingModel, selectiveOutputMask);

    const float roughness     = surface.b;
    const float roughnessFade =
        saturate((g_MaxRoughness - roughness) / (g_MaxRoughness * kRoughnessFadeRange));
    if (shadingModel == SHADINGMODELID_UNLIT || roughnessFade <= 0.0)
    {
        return 0.0;
    }

    const float2 uv            = (float2(px) + 0.5) * invSize;
    const float3 worldPosition = WorldPosition(view, uv, depth);
    const float3 eye           = mul(view.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    const float3 fromEye       = normalize(worldPosition - eye);
    const float3 normal        = normalize(DecodeNormal(g_GBufferNormal.Load(int3(px, 0)).xyz));
    const float3 reflected     = reflect(fromEye, normal);

    const float towardEyeFade = 1.0 - smoothstep(kTowardEyeFadeStart, kTowardEyeFadeEnd, dot(reflected, -fromEye));
    if (towardEyeFade <= 0.0)
    {
        return 0.0;
    }

    const ScreenTraceSpace space =
        ScreenTrace_MakeSpace(g_HZBUvFactor, g_HZBSize, g_HZBMipCount, uint2(size));

    float3 origin;
    float3 direction;
    ScreenTrace_MakeRay(view, space, uv, depth, worldPosition, reflected, origin, direction);

    bool hit;
    const float3 end = ScreenTrace_March(g_Depth, g_HZBClosest, space, origin, direction, g_StepCountMax, hit);
    if (!hit)
    {
        return 0.0;
    }

    const float2 hitUV = end.xy / space.uvFactor;
    const int2   hitPx = clamp(int2(hitUV * size), int2(0, 0), space.depthLast);

    // The ray hardly left: what it found is the surface it started on.
    if (all(abs(hitUV - uv) < 2.0 * invSize))
    {
        return 0.0;
    }

    // The sky is the environment's to reflect.
    const float hitDepth = g_Depth.Load(int3(hitPx, 0));
    if (hitDepth <= 0.0)
    {
        return 0.0;
    }

    // Came up behind a surface that faces away from the ray: the screen shows its other side.
    const float3 hitNormal = DecodeNormal(g_GBufferNormal.Load(int3(hitPx, 0)).xyz);
    if (dot(hitNormal, reflected) > 0.0)
    {
        return 0.0;
    }

    const float3 hitSurface = WorldPosition(view, hitUV, hitDepth);
    const float3 hitRay     = WorldPosition(view, hitUV, end.z);
    const float  confidence =
        ScreenTrace_ThicknessConfidence(distance(hitSurface, hitRay), kThickness * distance(hitSurface, eye))
        * ScreenTrace_BorderFade(hitUV, size);

    // The color of what was hit is in last frame's finished image, where that surface was then.
    // The ray ran through this frame's jittered image; velocity and the history are unjittered.
    const float2 unjitteredUV = hitUV - view.temporalAAJitter.xy * float2(0.5, -0.5);
    const float2 previousUV   = PreviousScreenUV(unjitteredUV, g_Velocity.Load(int3(hitPx, 0)));

    // Without it (the first frame, a target that changed size, a surface that was off screen)
    // this frame's color stands in: the hit as lit so far, lacking its own reflections. Selected,
    // not weighted: a history that is not valid may hold NaN.
    float3 radiance;
    if (g_HistoryValid != 0 && all(previousUV >= 0.0) && all(previousUV <= 1.0))
    {
        radiance = g_History.SampleLevel(g_LinearSampler, previousUV, 0).rgb;
    }
    else
    {
        radiance = g_SceneColor.SampleLevel(g_LinearSampler, hitUV, 0).rgb;
    }

    // Both are stored pre-exposed; last frame's pre-exposure is taken to equal this one's.
    radiance *= view.oneOverPreExposure;
    if (any(isnan(radiance)) || any(isinf(radiance)))
    {
        return 0.0;
    }

    return float4(radiance, confidence * roughnessFade * towardEyeFade * g_Intensity);
}

[numthreads(8, 8, 1)]
void CSMain(uint2 px : SV_DispatchThreadID)
{
    const ViewData view = GetView(g_Scope.viewIndex);

    // The dispatch covers the viewport in whole groups: the excess threads fall outside it.
    if (any(px >= uint2(view.bufferSizeAndInvSize.xy)))
    {
        return;
    }

    g_Output[px] = Reflection(px, view);
}
