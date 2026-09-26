// Writes an animated pattern into a render graph image it reaches by heap index.

struct PatternRootConstants
{
    uint  outputIndex;
    float time;
    uint2 size;
};

[[vk::push_constant]] ConstantBuffer<PatternRootConstants> g_Root : register(b0, space5);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    // Dispatch covers the image in whole groups: the excess threads fall outside it.
    if (any(id.xy >= g_Root.size))
    {
        return;
    }

    RWTexture2D<float4> output = ResourceDescriptorHeap[g_Root.outputIndex];

    const float2 uv = float2(id.xy) / float2(g_Root.size);
    const float  t  = g_Root.time;
    output[id.xy] = float4(
        0.5 + 0.5 * sin(uv.x * 12.0 + t),
        0.5 + 0.5 * sin(uv.y * 9.0 + t * 1.3),
        0.5 + 0.5 * sin((uv.x + uv.y) * 7.0 - t * 0.7),
        1.0);
}
