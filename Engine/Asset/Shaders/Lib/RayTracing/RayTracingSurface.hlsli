// Ray-hit adapter over shared geometry surfaces, plus default-lit material evaluation.
// See Render/RayTracing/README.md for binding/graph contracts.
#ifndef SPARK_LIB_RAY_TRACING_SURFACE_HLSLI
#define SPARK_LIB_RAY_TRACING_SURFACE_HLSLI

#include <Shaders/Lib/RayTracing/RayTracing.hlsli>
#include <Shaders/Lib/Geometry/GeometrySurface.hlsli>
#include <Shaders/Material/MaterialTemplate.hlsli>
#include <Shaders/Lib/ShadingCommon.hlsli>
#include <Shaders/Lib/BRDF/BRDF.hlsli>

typedef GeometrySurface RayTracingSurface;

// Precondition: a successful hit on this frame's engine TLAS, GeometryIndex == 0.
// Both sides are reconstructed; the geometric/shading frame faces the incoming ray,
// while WorldTriangleNormal retains the transformed index winding.
RayTracingSurface LoadRayTracingSurface(RayDesc ray, RayTracingHit hit)
{
    GeometrySurface surface = LoadGeometrySurface(hit.InstanceIndex, hit.PrimitiveIndex, hit.Barycentrics);
    // Preserve the traced hit position for ray offsets; arbitrary surface access uses
    // the barycentric position reconstructed from triangle vertices instead.
    surface.WorldPosition = ray.Origin + hit.T * ray.Direction;
    FaceGeometrySurface(surface, -ray.Direction);
    return surface;
}

struct RayTracingMaterial
{
    PixelMaterialInputs Inputs; // roughness remains unclamped; AO is data, not applied to traced light
    float3 WorldNormal;
    float3 DiffuseColor;
    float3 SpecularColor;
};

RayTracingMaterial EvaluateRayTracingMaterial(
    RayTracingSurface surface, SamplerState materialSampler, float mipLevel)
{
    MaterialPixelParameters parameters = surface.MaterialParameters;
    RayTracingMaterial material;
    CalcMaterialParametersLevel(parameters, material.Inputs, materialSampler, mipLevel);
    material.WorldNormal = parameters.WorldNormal;
    material.DiffuseColor = ComputeDiffuseColor(material.Inputs.BaseColor, material.Inputs.Metallic);
    material.SpecularColor = ComputeF0(material.Inputs.Specular, material.Inputs.BaseColor, material.Inputs.Metallic);
    return material;
}

// Same BRDF * NoL convention as deferred lighting. V and L point away from the surface.
// This is a surface response, not a complete lighting integral or a sampling PDF.
float3 EvaluateRayTracingBRDF(RayTracingMaterial material, float3 V, float3 L)
{
    // The reflection BRDF has support only above its shading hemisphere. Besides avoiding
    // wasted work, this prevents normalize(V + L) from producing NaN when L == -V.
    if (dot(material.WorldNormal, V) <= 0.0 || dot(material.WorldNormal, L) <= 0.0)
    {
        return 0.0;
    }
    return EvaluateBRDF(material.WorldNormal, V, L, material.DiffuseColor, material.SpecularColor,
                        max(material.Inputs.Roughness, 0.045));
}

#endif // SPARK_LIB_RAY_TRACING_SURFACE_HLSLI
