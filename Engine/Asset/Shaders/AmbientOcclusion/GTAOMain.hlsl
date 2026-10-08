// GTAOMain.hlsl — the second GTAO pass: the visibility of each pixel, and the depth edges
// around it for the denoiser. See XeGTAO_MainPass.

#include <Shaders/Lib/DeferredShadingCommon.hlsli>

struct ScopeParameters
{
    uint   viewIndex;
    uint   temporalNoise;
    float2 tanHalfFov;
    float  effectRadius;
    float  intensity;
    uint   depthIndex;          // the view-space depth chain, all its mips
    uint   normalIndex;         // GBufferNormal
    uint   outTermIndex;
    uint   outEdgesIndex;
    uint   sliceCount;          // directions integrated per pixel
};

#include <Shaders/ScopeBindings.hlsli>
#include <Shaders/AmbientOcclusion/GTAOCommon.hlsli>

SamplerState g_PointSampler : register(s0, space2);

[numthreads(8, 8, 1)]
void CSMain(uint2 pixCoord : SV_DispatchThreadID)
{
    const GTAOConstants consts = GetGTAOConstants();

    // The dispatch covers the viewport in whole groups: the excess threads fall outside it.
    if (any(pixCoord >= uint2(consts.ViewportSize)))
    {
        return;
    }

    Texture2D<float>   depth    = ResourceDescriptorHeap[g_Scope.depthIndex];
    Texture2D<float4>  normals  = ResourceDescriptorHeap[g_Scope.normalIndex];
    RWTexture2D<float> outTerm  = ResourceDescriptorHeap[g_Scope.outTermIndex];
    RWTexture2D<float> outEdges = ResourceDescriptorHeap[g_Scope.outEdgesIndex];

    // The GBuffer's normal is world space; the integration runs in view space.
    const float3 worldNormal = normalize(DecodeNormal(normals.Load(int3(pixCoord, 0)).xyz));
    const float3 viewNormal  = mul((float3x3)GetView(g_Scope.viewIndex).view, worldNormal);

    // 3 steps along each side of a slice, as the source's High and Ultra presets (3 and 9
    // slices). The steps stay fixed: more of them find more occlusion, not just less noise.
    XeGTAO_MainPass(
        pixCoord, float(g_Scope.sliceCount), 3, XeGTAO_SpatioTemporalNoise(pixCoord, consts.NoiseIndex),
        viewNormal, consts, depth, g_PointSampler, outTerm, outEdges);
}
