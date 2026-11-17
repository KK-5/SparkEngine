// Completes GBufferPass's Velocity into ResolvedVelocity, the one every temporal consumer reads.
// Surface pixels retain all four channels. Uncovered background pixels receive
// camera rotation only: an infinite sky has no parallax from camera translation.

#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Lib/Velocity.hlsli>

struct ScopeParameters
{
    uint viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

Texture2D<float4> g_Velocity : register(t0, space2);
Texture2D<float>  g_Depth    : register(t1, space2); // SceneDepth, viewed as R32_FLOAT

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    // Full-screen triangle from SV_VertexID: NDC (-1,-1),(3,-1),(-1,3).
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    int3 px = int3(int2(input.position.xy), 0);   // inputs share this pass's target

    const float4 velocity = g_Velocity.Load(px);
    if (IsVelocityWritten(velocity.xy))
    {
        return velocity;
    }

    // Missing geometry velocity is not sky just because the GBuffer was unwritten.
    const float deviceZ = g_Depth.Load(px);
    if (!isfinite(deviceZ) || deviceZ != 0.0)
    {
        return float4(kVelocityMax, kVelocityMax, 0.0, 0.0);
    }

    const ViewData view = GetView(g_Scope.viewIndex);
    const float2 viewUV = (input.position.xy - view.viewRectMin.xy) * view.viewSizeAndInvSize.zw;
    const float2 ndc = viewUV * float2(2.0, -2.0) + float2(-1.0, 1.0) - view.temporalAAJitter.xy;
    // invViewProj includes current jitter, so unproject the original raster NDC.
    const float4 farWorldH = mul(view.invViewProj, float4(ndc + view.temporalAAJitter.xy, 0.0, 1.0));
    const float3 eye = mul(view.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    // Homogeneous subtraction works for both finite and infinite far planes.
    const float3 direction = farWorldH.xyz - eye * farWorldH.w;
    const float4 previousClip = mul(view.prevViewProjection, float4(direction, 0.0));
    const float2 skyVelocity = CalcVelocity(float4(ndc, 0.0, 1.0), previousClip);
    return float4(skyVelocity, 0.0, 0.0);
}
