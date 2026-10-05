// ComputePattern.hlsl with a brightness: the same animated pattern, written where the
// dispatch reaches. One Scope covers the view dimly, another brightly over whatever an
// indirect dispatch covers.

#include <Shaders/ViewBindings.hlsli>

struct ScopeParameters
{
    uint  outputIndex;
    float time;
    uint  viewIndex;
    float brightness;
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
    const float3 color = float3(
        0.5 + 0.5 * sin(uv.x * 12.0 + t),
        0.5 + 0.5 * sin(uv.y * 9.0 + t * 1.3),
        0.5 + 0.5 * sin((uv.x + uv.y) * 7.0 - t * 0.7));
    output[id.xy] = float4(color * g_Scope.brightness, 1.0);
}
