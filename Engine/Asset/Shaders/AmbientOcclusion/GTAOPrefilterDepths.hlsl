// GTAOPrefilterDepths.hlsl — the first of the three GTAO passes: turns SceneDepth into a chain
// of view-space depths, five mips written in one dispatch. The main pass reads the far taps of
// a pixel from the smaller mips, which keeps them in cache and averages out their noise.
//
// The chain is a weighted average (XeGTAO_DepthMIPFilter), not a min or a max, so it is not
// the HZB.

struct ScopeParameters
{
    uint   viewIndex;
    uint   temporalNoise;
    float2 tanHalfFov;
    float  effectRadius;
    float  intensity;
    uint   sceneDepthIndex;
    uint   outDepthIndex0;      // one view per mip of the chain
    uint   outDepthIndex1;
    uint   outDepthIndex2;
    uint   outDepthIndex3;
    uint   outDepthIndex4;
};

#include <Shaders/ScopeBindings.hlsli>
#include <Shaders/AmbientOcclusion/GTAOCommon.hlsli>

SamplerState g_PointSampler : register(s0, space2);

// A thread covers 2x2 pixels of mip 0, so the dispatch is half the viewport a side.
[numthreads(8, 8, 1)]
void CSMain(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID)
{
    Texture2D<float>   sceneDepth = ResourceDescriptorHeap[g_Scope.sceneDepthIndex];
    RWTexture2D<float> outDepth0  = ResourceDescriptorHeap[g_Scope.outDepthIndex0];
    RWTexture2D<float> outDepth1  = ResourceDescriptorHeap[g_Scope.outDepthIndex1];
    RWTexture2D<float> outDepth2  = ResourceDescriptorHeap[g_Scope.outDepthIndex2];
    RWTexture2D<float> outDepth3  = ResourceDescriptorHeap[g_Scope.outDepthIndex3];
    RWTexture2D<float> outDepth4  = ResourceDescriptorHeap[g_Scope.outDepthIndex4];

    XeGTAO_PrefilterDepths16x16(
        dispatchThreadID, groupThreadID, GetGTAOConstants(), sceneDepth, g_PointSampler,
        outDepth0, outDepth1, outDepth2, outDepth3, outDepth4);
}
