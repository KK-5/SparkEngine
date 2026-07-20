// Tonemap.hlsl — final post-process pass. A full-screen triangle that samples the
// linear-HDR SceneColor (R16G16B16A16_FLOAT, written by the lighting + skybox passes),
// applies Reinhard tonemapping + gamma, and writes the LDR swap chain. This is the
// pivot from linear-HDR scene space to display-referred LDR; UIPass draws on top
// afterwards (UI is authored in display space and must NOT be tonemapped).
//
// SceneColor is read with Load (integer pixel fetch, 1:1, no sampler) — the natural
// fit for a full-res point read, matching the deferred lighting pass's GBuffer reads.
// This pass replaces the old CopyFrameBufferPass hardware blit: once HDR→LDR needs a
// shader, the tonemap draw can target the swap chain directly, so no separate copy.

Texture2D<float4> g_SceneColor : register(t0, space0);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    // Full-screen triangle from SV_VertexID: NDC (-1,-1),(3,-1),(-1,3). No depth test,
    // so z is arbitrary (0).
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv       = uv;
    return output;
}

float3 Tonemap(float3 hdr)
{
    float3 mapped = hdr / (hdr + 1.0);  // Reinhard
    return pow(mapped, 1.0 / 2.2);      // linear -> sRGB-ish
}

float4 PSMain(VSOutput input) : SV_Target0
{
    int3 px = int3(int2(input.position.xy), 0);
    float3 hdr = g_SceneColor.Load(px).rgb;
    return float4(Tonemap(hdr), 1.0);
}
