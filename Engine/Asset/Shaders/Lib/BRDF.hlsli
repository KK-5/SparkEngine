// Cook-Torrance BRDF library — pure surface response, no light and no bindings.
// Given the shading frame (N, V, L) and material params it returns an outgoing
// radiance factor; the caller scales by the light's incident radiance. Reusable by
// any pass that shades a surface (deferred lighting, forward, IBL reuses D/G/F).
#ifndef SPARK_LIB_BRDF_HLSLI
#define SPARK_LIB_BRDF_HLSLI

static const float PI = 3.14159265359;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom  = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Cook-Torrance BRDF for one light. L points toward the light; returns the outgoing
// radiance factor (diffuse + specular) * NdotL, to be scaled by the light's radiance.
float3 EvaluateBRDF(float3 N, float3 V, float3 L, float3 albedo, float roughness, float metallic, float3 F0)
{
    float3 H = normalize(V + L);

    float  D = DistributionGGX(N, H, roughness);
    float  G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float  NdotV = max(dot(N, V), 0.0);
    float  NdotL = max(dot(N, L), 0.0);
    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);

    float3 kd = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kd * albedo / PI;

    return (diffuse + specular) * NdotL;
}

#endif // SPARK_LIB_BRDF_HLSLI
