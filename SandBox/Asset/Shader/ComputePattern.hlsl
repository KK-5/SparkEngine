// Writes an animated pattern into a render graph image it reaches by heap index, sized by
// the view it picks from g_Views.

#include <Shaders/ViewBindings.hlsli>

struct ScopeParameters
{
    uint  outputIndex;
    float time;
    uint  viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint2 size = uint2(GetView(g_Scope.viewIndex).viewSizeAndInvSize.xy);

    // Dispatch covers the image in whole groups: the excess threads fall outside it.
    if (any(id.xy >= size))
    {
        return;
    }

    RWTexture2D<float4> output = ResourceDescriptorHeap[g_Scope.outputIndex];

    const float2 uv = float2(id.xy) / float2(size);
    const float  t  = g_Scope.time;
    output[id.xy] = float4(
        0.5 + 0.5 * sin(uv.x * 12.0 + t),
        0.5 + 0.5 * sin(uv.y * 9.0 + t * 1.3),
        0.5 + 0.5 * sin((uv.x + uv.y) * 7.0 - t * 0.7),
        1.0);
}
