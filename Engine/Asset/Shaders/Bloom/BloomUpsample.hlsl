// BloomUpsample.hlsl — one step of the bloom upsample chain. BloomPass dispatches it once per
// level, smallest first: the glow accumulated so far (one level smaller) is upsampled and the
// scene downsample level of this size is added to it.
//
//     glow(j) = level(j) + Tent(glow(j + 1))
//
// Unrolled, glow(1) is the sum of every level: blurs whose widths double from one level to the
// next. Summed with equal weight they make a kernel that falls off near 1/r^2, the long tail a
// lens scatters light into. TonemapPass divides by the level count.
//
// The upsample is a 3x3 tent (1 2 1 / 2 4 2 / 1 2 1, over 16) of bilinear taps one low-res
// texel apart. Plain bilinear magnification of a small level shows its texel grid as diamond
// blocks; the tent smooths it so the levels blend into one another.

struct BloomUpsampleRootConstants
{
    float2 lowInvSize;
    uint2  outputSize;
    uint   currentIndex;
    uint   lowIndex;
    uint   outputIndex;
};

[[vk::push_constant]] ConstantBuffer<BloomUpsampleRootConstants> g_Root : register(b0, space5);

SamplerState g_LinearSampler : register(s0, space2);

float3 Tap(Texture2D<float4> low, float2 uv, float2 offset)
{
    return low.SampleLevel(g_LinearSampler, uv + offset * g_Root.lowInvSize, 0).rgb;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    // The dispatch covers the level in whole groups: the excess threads fall outside it.
    if (any(id.xy >= g_Root.outputSize))
    {
        return;
    }

    Texture2D<float4>   current = ResourceDescriptorHeap[g_Root.currentIndex];
    Texture2D<float4>   low     = ResourceDescriptorHeap[g_Root.lowIndex];
    RWTexture2D<float4> output  = ResourceDescriptorHeap[g_Root.outputIndex];

    const float2 uv = (float2(id.xy) + 0.5) / float2(g_Root.outputSize);
    const float3 glow =
        ( Tap(low, uv, float2(-1.0, -1.0))       + Tap(low, uv, float2(0.0, -1.0)) * 2.0 + Tap(low, uv, float2(1.0, -1.0))
        + Tap(low, uv, float2(-1.0,  0.0)) * 2.0 + Tap(low, uv, float2(0.0,  0.0)) * 4.0 + Tap(low, uv, float2(1.0,  0.0)) * 2.0
        + Tap(low, uv, float2(-1.0,  1.0))       + Tap(low, uv, float2(0.0,  1.0)) * 2.0 + Tap(low, uv, float2(1.0,  1.0))
        ) * (1.0 / 16.0);

    // Same size as the output: one texel per thread, no filtering.
    output[id.xy] = float4(current.Load(int3(id.xy, 0)).rgb + glow, 1.0);
}
