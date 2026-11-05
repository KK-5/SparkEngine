#include <Shaders/ViewBindings.hlsli>
#include <Shaders/RayTracingSceneBindings.hlsli>

struct ScopeParameters
{
    uint viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    const ViewData view = GetView(g_Scope.viewIndex);
    const float2 uv = (input.position.xy - view.viewRectMin.xy) * view.viewSizeAndInvSize.zw;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    // Reversed-Z: unproject the near plane, avoiding the far plane's possible w == 0.
    const float4 nearPoint = mul(view.invViewProj, float4(ndc, 1.0, 1.0));

    RayDesc ray;
    ray.Origin = mul(view.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    ray.Direction = normalize(nearPoint.xyz / nearPoint.w - ray.Origin);
    ray.TMin = 0.001;
    ray.TMax = 100.0;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(g_RayTracingScene, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed()) {}

    float3 color = float3(0.025, 0.035, 0.055);
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        uint hash = (query.CommittedInstanceID() + 1u) * 0x9E3779B9u;
        hash ^= hash >> 16;
        color = 0.2 + 0.8 * float3(hash & 255u, (hash >> 8) & 255u, (hash >> 16) & 255u) / 255.0;
    }
    return float4(color, 1.0);
}
