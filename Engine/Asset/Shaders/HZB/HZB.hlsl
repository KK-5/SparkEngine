// HZB.hlsl — one mip of the hierarchical Z-buffer: each texel takes the closest and the
// furthest depth of the 2x2 texels under it. HZBPass dispatches it once per mip, each a Scope
// reaching its inputs and outputs by heap index; mip 0 reads the scene depth for both chains.
//
// Depth is reversed-Z: the closest is the max, the furthest the min.
//
// The chain's sides are powers of two, so mip 0 is wider than the half-size scene depth and
// the coordinates clamp to the input. A texel past it repeats the input's edge, which changes
// neither the max nor the min of the texels above it.

struct ScopeParameters
{
    uint  closestInputIndex;
    uint  furthestInputIndex;
    uint  closestOutputIndex;
    uint  furthestOutputIndex;
    uint2 inputSize;
    uint2 outputSize;
};

#include <Shaders/ScopeBindings.hlsli>

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    // The dispatch covers the mip in whole groups: the excess threads fall outside it.
    if (any(id.xy >= g_Scope.outputSize))
    {
        return;
    }

    // Each view is one mip of its image, so the mip to load is the view's 0.
    Texture2D<float>   closestInput   = ResourceDescriptorHeap[g_Scope.closestInputIndex];
    Texture2D<float>   furthestInput  = ResourceDescriptorHeap[g_Scope.furthestInputIndex];
    RWTexture2D<float> closestOutput  = ResourceDescriptorHeap[g_Scope.closestOutputIndex];
    RWTexture2D<float> furthestOutput = ResourceDescriptorHeap[g_Scope.furthestOutputIndex];

    const uint2 last = g_Scope.inputSize - 1;

    float closest  = 0.0;
    float furthest = 1.0;
    for (uint i = 0; i < 4; ++i)
    {
        const uint2 coord = min(id.xy * 2 + uint2(i & 1, i >> 1), last);
        closest  = max(closest,  closestInput.Load(int3(coord, 0)));
        furthest = min(furthest, furthestInput.Load(int3(coord, 0)));
    }

    closestOutput[id.xy]  = closest;
    furthestOutput[id.xy] = furthest;
}
