// Environment BRDF — the split-sum approximation's second factor: the specular BRDF
// integrated against a white environment, returned as the scale/bias pair applied to F0.
//
//     specular = prefilteredRadiance * EnvBRDFApprox(F0, perceptualRoughness, NoV)
//
// Lazarov's analytic fit (Black Ops II, SIGGRAPH 2011) standing in for a pre-integrated
// DFG table. Swapping in a real table later only changes this file's body.
#ifndef SPARK_LIB_ENV_BRDF_HLSLI
#define SPARK_LIB_ENV_BRDF_HLSLI

//! perceptualRoughness, NOT the linear alpha that D_GGX / V_SmithGGXCorrelated take.
//!
//! Known bias: fitted against UE4's non-height-correlated Smith + k = alpha/2 IBL remap,
//! while EvaluateBRDF uses V_SmithGGXCorrelated — so IBL specular runs systematically a
//! few percent off the direct-light specular beside it. A DFG table integrated against
//! V_SmithGGXCorrelated is what would remove it.
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
