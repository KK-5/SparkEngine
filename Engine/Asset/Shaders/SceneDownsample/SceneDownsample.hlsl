// SceneDownsample.hlsl — one level of the scene color downsample chain: filters the level
// above (or the scene color itself) down to half its size. SceneDownsamplePass dispatches it
// once per level, each a Scope reaching its input and output by heap index.
//
// The filter is Jimenez's 13-tap (Next Generation Post Processing in Call of Duty: Advanced
// Warfare): five overlapping 4x4 boxes, the centre one weighted 0.5 and the four corner ones
// 0.125 each, read with 13 bilinear taps that each average 2x2 texels. A plain 2x2 average is
// a poor low-pass: a small highlight moving by one pixel jumps between blocks and the level
// below flickers. The wider, smoother footprint changes continuously instead.

struct SceneDownsampleRootConstants
{
    uint   inputIndex;
    uint   outputIndex;
    float2 inputInvSize;
    uint2  outputSize;
};

[[vk::push_constant]] ConstantBuffer<SceneDownsampleRootConstants> g_Root : register(b0, space5);

SamplerState g_LinearSampler : register(s0, space2);

float3 Tap(Texture2D<float4> input, float2 uv, float2 offset)
{
    return input.SampleLevel(g_LinearSampler, uv + offset * g_Root.inputInvSize, 0).rgb;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    // The dispatch covers the level in whole groups: the excess threads fall outside it.
    if (any(id.xy >= g_Root.outputSize))
    {
        return;
    }

    Texture2D<float4>   input  = ResourceDescriptorHeap[g_Root.inputIndex];
    RWTexture2D<float4> output = ResourceDescriptorHeap[g_Root.outputIndex];

    // The output pixel's centre sits on a texel corner of the input. Offsets are in input
    // texels; each tap lands between four texels and averages them.
    //   a . b . c
    //   . d . e .
    //   f . g . h
    //   . i . j .
    //   k . l . m
    const float2 uv = (float2(id.xy) + 0.5) / float2(g_Root.outputSize);
    const float3 a = Tap(input, uv, float2(-2.0, -2.0));
    const float3 b = Tap(input, uv, float2( 0.0, -2.0));
    const float3 c = Tap(input, uv, float2( 2.0, -2.0));
    const float3 d = Tap(input, uv, float2(-1.0, -1.0));
    const float3 e = Tap(input, uv, float2( 1.0, -1.0));
    const float3 f = Tap(input, uv, float2(-2.0,  0.0));
    const float3 g = Tap(input, uv, float2( 0.0,  0.0));
    const float3 h = Tap(input, uv, float2( 2.0,  0.0));
    const float3 i = Tap(input, uv, float2(-1.0,  1.0));
    const float3 j = Tap(input, uv, float2( 1.0,  1.0));
    const float3 k = Tap(input, uv, float2(-2.0,  2.0));
    const float3 l = Tap(input, uv, float2( 0.0,  2.0));
    const float3 m = Tap(input, uv, float2( 2.0,  2.0));

    // Centre box d e i j at 0.5; corner boxes (a b f g), (b c g h), (f g k l), (g h l m) at
    // 0.125. Folded per tap: a tap shared by n boxes carries n of their quarter-weights.
    float3 color = (d + e + i + j) * (0.5   / 4.0)
                 + (a + c + k + m) * (0.125 / 4.0)
                 + (b + f + h + l) * (0.125 / 4.0 * 2.0)
                 + g               * (0.125 / 4.0 * 4.0);

    // One NaN or Inf pixel would spread down the chain into a whole block of bloom. Only the
    // first level can meet one; checking every level spares a branch on which level this is.
    color = all(isfinite(color)) ? color : float3(0.0, 0.0, 0.0);

    output[id.xy] = float4(color, 1.0);
}
