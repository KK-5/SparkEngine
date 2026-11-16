// Cook-Torrance BRDF library — pure surface response, no light and no bindings.
// Given the shading frame (N, V, L) and material params it returns an outgoing
// radiance factor; the caller scales by the light's incident radiance. Reusable by
// any pass that shades a surface (deferred lighting, forward, IBL reuses D/V/F).
#ifndef SPARK_LIB_BRDF_HLSLI
#define SPARK_LIB_BRDF_HLSLI

#include "Diffuse.hlsli"
#include "Distribution.hlsli"
#include "Schlick.hlsli"
#include "Visibility.hlsli"

// Cook-Torrance BRDF for one light. L points toward the light; returns the outgoing
// radiance factor (diffuse + specular) * NoL, to be scaled by the light's radiance.
// Precondition: perceptualRoughness must be clamped to >= 0.045 by the caller (matches
// Filament — the material stage clamps, the BRDF assumes valid input). Below that the
// visibility term's 0.5 / (GGXV + GGXL) can divide by zero at grazing angles.
struct BRDFResponse
{
    float3 Diffuse;
    float3 Specular;
};

// Both lobes include NoL. Keeping them separate lets lighting signals be denoised
// independently without changing the BRDF used by the reference integrator.
BRDFResponse EvaluateBRDFLobes(float3 N, float3 V, float3 L, float3 diffuseColor, float3 F0, float perceptualRoughness)
{
    float3 H = normalize(V + L);
    float NoV = max(abs(dot(N, V)), 1e-4);   // floor to avoid /0; must stay <= 1 or F_Schlick's pow() gets a negative base -> NaN
    float NoL = saturate(dot(N, L));
    float NoH = saturate(dot(N, H));
    float LoH = saturate(dot(L, H));

    float roughness = perceptualRoughness * perceptualRoughness;

    float D = D_GGX(NoH, roughness);
    float3 F = F_Schlick(LoH, F0);
    float Vis = V_SmithGGXCorrelated(NoV, NoL, roughness);

    // specular BRDF
    float3 Fr = (D * Vis) * F;

    // diffuse BRDF
    float3 Fd = diffuseColor * Fd_Burley(NoV, NoL, LoH, roughness);

    BRDFResponse response;
    response.Diffuse = Fd * NoL;
    response.Specular = Fr * NoL;
    return response;
}

float3 EvaluateBRDF(float3 N, float3 V, float3 L, float3 diffuseColor, float3 F0, float perceptualRoughness)
{
    const BRDFResponse response = EvaluateBRDFLobes(N, V, L, diffuseColor, F0, perceptualRoughness);
    return response.Diffuse + response.Specular;
}

#endif // SPARK_LIB_BRDF_HLSLI
