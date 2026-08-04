// Importance sampling for the split-sum bakes (specular prefilter, BRDF LUT).
//
// Shared so the two bakes cannot drift on the roughness convention: this takes
// PERCEPTUAL roughness and derives alpha internally.
#ifndef SPARK_LIB_IMPORTANCE_SAMPLE_HLSLI
#define SPARK_LIB_IMPORTANCE_SAMPLE_HLSLI

#include <Shaders/Lib/BRDF/Constants.hlsli>

//! GGX-distributed half vector around N. `perceptualRoughness`, NOT alpha.
float3 ImportanceSampleGGX(float2 xi, float3 N, float perceptualRoughness)
{
    float a = perceptualRoughness * perceptualRoughness;
    // Guard the a=0 mirror singularity: xi.y can round to exactly 1.0 -> 0/0 = NaN, which
    // poisons the whole sum. max()/saturate keep cosThetaH finite (a=0 still gives H = N).
    float denom = 1.0 + (a * a - 1.0) * xi.y;
    float cosThetaH = sqrt(saturate((1.0 - xi.y) / max(denom, 1e-8)));
    float sinThetaH = sqrt(saturate(1.0 - cosThetaH * cosThetaH));
    float phi = 2.0 * PI * xi.x;
    float3 tangentDir = float3(sinThetaH * cos(phi), sinThetaH * sin(phi), cosThetaH);

    float3 up    = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 right = normalize(cross(up, N));
    up           = normalize(cross(N, right));

    return tangentDir.x * right + tangentDir.y * up + tangentDir.z * N;
}

#endif // SPARK_LIB_IMPORTANCE_SAMPLE_HLSLI
