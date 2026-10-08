// GTAODenoise.hlsl — the third GTAO pass: blurs the main pass's term once (the source's
// "sharp" denoise) without crossing the depth edges it recorded, and applies the intensity.
// See XeGTAO_Denoise.

struct ScopeParameters
{
    uint   viewIndex;
    uint   temporalNoise;
    float2 tanHalfFov;
    float  effectRadius;
    float  intensity;
    uint   termIndex;
    uint   edgesIndex;
    uint   outputIndex;
};

#include <Shaders/ScopeBindings.hlsli>
#include <Shaders/AmbientOcclusion/GTAOCommon.hlsli>

SamplerState g_PointSampler : register(s0, space2);

// A thread computes two horizontal pixels, so the dispatch is half the viewport wide.
[numthreads(8, 8, 1)]
void CSMain(uint2 dispatchThreadID : SV_DispatchThreadID)
{
    Texture2D<float>   term   = ResourceDescriptorHeap[g_Scope.termIndex];
    Texture2D<float>   edges  = ResourceDescriptorHeap[g_Scope.edgesIndex];
    RWTexture2D<float> output = ResourceDescriptorHeap[g_Scope.outputIndex];

    const uint2 pixCoordBase = dispatchThreadID * uint2(2, 1);
    XeGTAO_Denoise(pixCoordBase, GetGTAOConstants(), term, edges, g_PointSampler, output, true);
}
