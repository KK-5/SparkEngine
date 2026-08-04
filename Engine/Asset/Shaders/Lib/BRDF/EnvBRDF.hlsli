// Environment BRDF — the split-sum approximation's second factor: the specular BRDF
// integrated against a white environment, returned as the scale/bias pair applied to F0.
//
//     specular = prefilteredRadiance * EnvBRDFApprox(F0, perceptualRoughness, NoV)
//
#ifndef SPARK_LIB_ENV_BRDF_HLSLI
#define SPARK_LIB_ENV_BRDF_HLSLI

//! Table lookup — the production path. The table is passed in rather than read from
//! space0 so this stays a pure library (a BRDF header must not drag scene bindings into
//! every shader that includes it); the caller supplies g_BRDFLut + g_IBLSampler.
//!
//! uv = (NoV, perceptualRoughness), matching BRDFLutBake.hlsl's texel-centre mapping.
//! Both sides must agree and nothing fails if they do not.
float3 EnvBRDFLut(Texture2D lut, SamplerState lutSampler,
                  float3 F0, float perceptualRoughness, float NoV)
{
    float2 AB = lut.SampleLevel(lutSampler, float2(NoV, perceptualRoughness), 0.0).rg;
    return F0 * AB.x + AB.y;
}

//! Lazarov's analytic fit (Black Ops II, SIGGRAPH 2011). NOT used for rendering — kept as
//! the reference EnvBRDFLut is A/B'd against, since it is the only other implementation of
//! this quantity in the tree.
//!
//! Do not quietly swap it back in: it is fitted against UE4's non-height-correlated Smith
//! plus the k = alpha/2 IBL remap, while the table is integrated against this repo's
//! V_SmithGGXCorrelated. Measured against the table it runs ~15-20% LOW at mid roughness
//! and ~45% HIGH at roughness 1 — not a bias a constant could correct.
float3 EnvBRDFApprox(float3 F0, float perceptualRoughness, float NoV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572,  0.022);
    const float4 c1 = float4( 1.0,  0.0425,  1.040, -0.040);

    float4 r    = perceptualRoughness * c0 + c1;
    float  a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 AB   = float2(-1.04, 1.04) * a004 + r.zw;

    return F0 * AB.x + AB.y;
}

#endif // SPARK_LIB_ENV_BRDF_HLSLI
