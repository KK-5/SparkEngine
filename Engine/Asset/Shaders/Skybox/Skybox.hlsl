// Skybox.hlsl — full-screen triangle sampling a baked environment cubemap.
//
// No vertex input: the VS synthesizes a full-screen triangle from SV_VertexID and
// places it on the far plane. The clip space is left-handed, zero-to-one depth
// (glm::perspectiveLH_ZO), so the far plane is NDC z = 1 — emitted as z = w. With the
// pass depth test LessEqual and SceneDepth cleared to 1.0, the sky only survives where
// no opaque geometry wrote a nearer depth (today there is none, so it fills the frame).
//
// The PS reconstructs the per-pixel world-space view ray as (far-plane point - camera
// position): the far point is un-projected via g_InvViewProj, the camera position is the
// world-space image of the view-space origin (mul(g_InvView, origin)). The cube is
// sampled with the D3D TextureCube convention — the same basis the bake shader
// (EnvironmentBake.hlsl) used — so Sample(dir) round-trips to the equirect texel with no
// extra pairing. The raw sample is written as linear HDR into SceneColor (R16F); the
// dedicated TonemapPass tonemaps the whole scene at the end.
//
// All view matrices come from the shared per-view tier (ViewBindings, space1), computed
// once per frame by WriteViewConstants — the skybox never re-reads the camera. The cube +
// sampler are this pass's own inputs, in the per-pass tier (space2).

#include <Shaders/ViewBindings.hlsl>   // space1: g_ViewProjection, g_InvViewProj, g_View, g_InvView

// Per-pass inputs (space2 = per-pass tier), bound by SkyboxProcessor.
TextureCube  g_SkyCube    : register(t0, space2);
SamplerState g_SkySampler : register(s0, space2);

struct VSOutput
{
    float4 position : SV_Position;
    float2 ndc      : TEXCOORD0;  // clip-space xy (== NDC here, since w == 1)
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    // Full-screen triangle: uv (0,0),(2,0),(0,2) -> NDC (-1,-1),(3,-1),(-1,3).
    float2 uv  = float2((vertexId << 1) & 2, vertexId & 2);
    float2 ndc = uv * 2.0 - 1.0;

    VSOutput output;
    output.position = float4(ndc, 1.0, 1.0);  // z = w -> NDC z = 1 (far plane)
    output.ndc      = ndc;
    return output;
}

float3 ReconstructWorldDir(float2 ndc)
{
    // Camera world position = view-space origin mapped to world (translation of g_InvView,
    // extracted convention-safely via mul rather than indexing a column).
    float3 eye = mul(g_InvView, float4(0.0, 0.0, 0.0, 1.0)).xyz;

    // Far-plane (z=1) clip point un-projected to world; the ray is far - eye.
    float4 farWorld = mul(g_InvViewProj, float4(ndc, 1.0, 1.0));
    farWorld /= farWorld.w;
    return normalize(farWorld.xyz - eye);
}

float4 PSMain(VSOutput input) : SV_Target0
{
    float3 dir   = ReconstructWorldDir(input.ndc);
    float3 color = g_SkyCube.Sample(g_SkySampler, dir).rgb;

    return float4(color, 1.0);
}
