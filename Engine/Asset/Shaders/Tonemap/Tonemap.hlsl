// Tonemap.hlsl — final post-process pass. A full-screen triangle that samples the
// linear-HDR SceneColor (R16G16B16A16_FLOAT, written by the lighting + skybox passes),
// applies exposure -> tone curve -> display encoding, and writes the LDR swap chain.
// This is the pivot from linear-HDR scene space to display-referred LDR; UIPass draws
// on top afterwards (UI is authored in display space and must NOT be tonemapped).
//
// The chain is three ordered, independent stages: (1) exposure — a linear scale that
// picks which slice of the HDR range the camera is sensitive to; (2) the tone curve —
// the non-linear compression into [0,1] (Reinhard today, ACES later); (3) the OETF —
// display encoding (gamma). They are kept separate so the tone operator can change
// without touching exposure or gamma.
//
// SceneColor is read with Load (integer pixel fetch, 1:1, no sampler) — the natural
// fit for a full-res point read, matching the deferred lighting pass's GBuffer reads.
// This pass replaces the old CopyFrameBufferPass hardware blit: once HDR→LDR needs a
// shader, the tonemap draw can target the swap chain directly, so no separate copy.

#include <Shaders/ViewBindings.hlsli>   // space1: g_Exposure

// Per-pass inputs (space2 = per-pass tier), declared by TonemapPass's Scope.
Texture2D<float4> g_SceneColor    : register(t0, space2);
Texture2D<float4> g_Bloom         : register(t1, space2);   // BloomPass's glow, half the render size
SamplerState      g_LinearSampler : register(s0, space2);

cbuffer TonemapParams : register(b0, space2)
{
    float g_SceneWeight;   // 1 - bloom intensity
    float g_BloomWeight;   // bloom intensity / bloom level count; 0 without bloom
};

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

// Tone curve: compress exposed linear HDR into [0,1]. Reinhard for now — swap for ACES
// later. Exposure lives upstream of this, so the operator can change independently.
float3 ToneCurve(float3 hdr)
{
    return hdr / (hdr + 1.0);  // Reinhard
}

// Display encoding (OETF): linear -> sRGB-ish. Kept separate from the tone curve so
// changing the operator never touches gamma (and this can later defer to an sRGB
// swap chain that applies the transfer function in hardware).
float3 OETF(float3 linearColor)
{
    return pow(linearColor, 1.0 / 2.2);
}

float4 PSMain(VSOutput input) : SV_Target0
{
    int3 px = int3(int2(input.position.xy - g_ViewRectMin.xy + g_InputViewRectMin.xy), 0);
    float3 hdr = g_SceneColor.Load(px).rgb * g_SceneWeight;

    // Uniform across the draw. Without bloom g_Bloom is not bound, so it must not be read.
    if (g_BloomWeight > 0.0)
    {
        // The glow covers the whole input buffer, at a fraction of its size.
        const float2 uv = (float2(px.xy) + 0.5) * g_InputBufferSizeAndInvSize.zw;
        hdr += g_Bloom.SampleLevel(g_LinearSampler, uv, 0).rgb * g_BloomWeight;
    }

    // Out of the PreExposure domain, the glow included; g_Exposure is the artistic scale, this is not.
    hdr *= g_OneOverPreExposure;

    hdr *= g_Exposure;                 // (1) exposure: linear scale before the tone curve
    float3 mapped = ToneCurve(hdr);    // (2) tone curve
    return float4(OETF(mapped), 1.0);  // (3) display encoding
}
