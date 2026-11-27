// Direct-light estimation and emitter-hit MIS for the current opaque reflection
// model. Uses engine scene bindings, but has no pass constants or RNG policy.
#ifndef SPARK_LIB_RAY_TRACING_LIGHTING_HLSLI
#define SPARK_LIB_RAY_TRACING_LIGHTING_HLSLI

#include <Shaders/Lib/RayTracing/RayTracingSurface.hlsli>
#include <Shaders/Lib/Sample/LightSampling.hlsli>
#include <Shaders/Lib/Sample/LightIntersection.hlsli>
#include <Shaders/Lib/Sample/MIS.hlsli>

struct RayTracingLightingSettings
{
    float RayBias; // world-space origin offset and finite shadow endpoint exclusion
    float MaxDistance; // shadow rays to infinite lights
    uint InstanceMask; // must match the scene used by scattering continuations
};

// Actual technique allocations at a vertex, not material capabilities. A missing
// technique has count zero. With more than one sample, the caller must average
// each technique's contributions over its own sample count.
struct LightingSampleCounts
{
    uint LightSampleCount;
    uint BRDFSampleCount;
};

bool MakeSampledLightShadowRay(GeometrySurface surface, LightSample lightSample,
                               RayTracingLightingSettings settings, float3 shadowOriginOffset, out RayDesc shadowRay)
{
    shadowRay = (RayDesc)0;
    if (!lightSample.CastShadow)
    {
        return false;
    }

    // Preserve the receiver position used by sampling, PDFs and raw hit distance.
    shadowRay.Origin = surface.WorldPosition + shadowOriginOffset
                    + surface.WorldGeometricNormal * settings.RayBias;
    shadowRay.TMin = 0.0;
    if (lightSample.IsInfinite)
    {
        shadowRay.Direction = lightSample.Direction;
        shadowRay.TMax = settings.MaxDistance;
    }
    else
    {
        // Re-aim after offsetting the origin and exclude the sampled endpoint,
        // so the emitting surface does not count as its own occluder.
        const float3 toLight = lightSample.Position - shadowRay.Origin;
        const float distanceToLight = length(toLight);
        if (distanceToLight <= settings.RayBias)
        {
            return false;
        }
        shadowRay.Direction = toLight / distanceToLight;
        shadowRay.TMax = distanceToLight - settings.RayBias;
    }
    return true;
}

bool TraceSampledLightVisibility(GeometrySurface surface, LightSample lightSample,
                                 RayTracingLightingSettings settings, float3 shadowOriginOffset)
{
    RayDesc shadowRay;
    if (!MakeSampledLightShadowRay(surface, lightSample, settings, shadowOriginOffset, shadowRay))
    {
        return true;
    }
    return TraceOpaqueVisibility(shadowRay, settings.InstanceMask);
}

bool TraceSampledLightVisibility(GeometrySurface surface, LightSample lightSample,
                                 RayTracingLightingSettings settings)
{
    return TraceSampledLightVisibility(surface, lightSample, settings, float3(0.0, 0.0, 0.0));
}

// ray.Origin, context.WorldPosition and surface.WorldPosition are the original
// receiver; ray.Direction is the sampled unit direction. Enumerates every visible
// emitting rectangle, not just the nearest one. Its interval must not be clipped
// by a scene hit: a rectangle with CastShadow == false remains visible behind it.
bool QueryNextVisibleRectLight(GeometrySurface surface, LightSamplingContext context, RayDesc ray,
                               SamplerState materialSampler, RayTracingLightingSettings settings,
                               float3 shadowOriginOffset, inout uint nextLightIndex,
                               out LightSample lightSample)
{
    lightSample = (LightSample)0;
    LightSampleLocation location;
    float hitT;
    [loop]
    while (FindNextRectLightIntersection(ray, nextLightIndex, location, hitT))
    {
        LightSample candidate;
        if (!ReconstructLightSample(context, location, GetLight(location.LightSlotId), materialSampler, candidate))
        {
            continue;
        }
        if (!any(candidate.Radiance > 0.0))
        {
            continue;
        }
        // Re-aim the corrected visibility ray at this exact light-surface point,
        // as NEE does. No offset is applied to its BRDF direction or sampling PDF.
        if (!TraceSampledLightVisibility(surface, candidate, settings, shadowOriginOffset))
        {
            continue;
        }
        // Discovery does not opt an integrator into rectangle MIS. The estimator
        // must actually allocate the matching BRDF technique before using it.
        lightSample = candidate;
        return true;
    }
    return false;
}

LightSampleLocation MakeEmissiveLightSampleLocation(RayTracingHit hit, uint lightSlotId)
{
    LightSampleLocation location = (LightSampleLocation)0;
    location.LightSlotId = lightSlotId;
    location.PrimitiveIndex = hit.PrimitiveIndex;
    location.Coordinates = hit.Barycentrics;
    return location;
}

// Precondition: a successful closest scene hit. The caller retains its hit/T
// even if this surface does not emit, so a distance guide needs no extra trace.
// No ordinary-surface reflection is evaluated. Ray traversal and its origin
// policy remain explicit at the caller, separate from light reconstruction.
bool ReconstructEmissiveLightSample(LightSamplingContext context, RayTracingHit hit,
                                    SamplerState materialSampler, out LightSample lightSample)
{
    lightSample = (LightSample)0;
    const InstanceData instance = GetInstanceData(hit.InstanceIndex);
    const LightSampleLocation location = MakeEmissiveLightSampleLocation(hit, instance.LightSlotId);
    LightSample candidate;
    if (location.LightSlotId == INVALID_LIGHT_SLOT)
    {
        // A hit still emits if its instance has no entry in the light sampler.
        const MaterialData material = GetMaterialData(instance.MaterialIndex);
        if (!any(material.Emissive.rgb * material.Emissive.a > 0.0))
        {
            return false;
        }
        EmissiveLightData light;
        light.model = instance.Model;
        light.geometry = g_Geometries[instance.GeometryIndex];
        light.color = material.Emissive.rgb;
        light.intensity = material.Emissive.a;
        light.textureIndex = material.TexIndices[SPARK_TEX_SLOT_EMISSIVE];
        if (!ReconstructEmissiveMeshSample(context, location, light, materialSampler, candidate))
        {
            return false;
        }
    }
    else if (!ReconstructLightSample(context, location, GetLight(location.LightSlotId), materialSampler, candidate))
    {
        return false;
    }
    if (!any(candidate.Radiance > 0.0))
    {
        return false;
    }
    lightSample = candidate;
    return true;
}

// One next-event-estimation sample. context.WorldPosition must be the receiver's
// original surface position; ray offsets affect visibility only. randomSample is
// supplied by the caller, independently of BRDF samples, and uniform in [0,1)^4.
struct DirectLightingSample
{
    float3 Diffuse;
    float3 Specular;
    // World-space guide distance. EvaluateDirectLightingSample records the light
    // distance even under occlusion. Zero means no distance was recorded.
    float HitDistance;
};

DirectLightingSample EvaluateDirectLightingSample(GeometrySurface surface, RayTracingMaterial material,
    float3 V, LightSamplingContext context, StructuredBuffer<float> weightTree, uint leafBase,
    float4 randomSample, SamplerState materialSampler, RayTracingLightingSettings settings,
    LightingSampleCounts sampleCounts, float3 shadowOriginOffset)
{
    DirectLightingSample result = (DirectLightingSample)0;
    if (sampleCounts.LightSampleCount == 0u)
    {
        return result;
    }
    LightSample lightSample;
    float selectionPmf;
    if (!SampleLight(context, weightTree, leafBase, randomSample, materialSampler, lightSample, selectionPmf))
    {
        return result;
    }
    // A corrected ray origin approximates the smooth vertex-normal surface. The
    // original triangle's hemisphere must not reject it before tracing visibility.
    // Uncorrected surfaces retain the geometric check, including reference paths.
    if (!any(shadowOriginOffset != 0.0) && dot(surface.WorldGeometricNormal, lightSample.Direction) <= 0.0)
    {
        return result;
    }
    const BRDFResult brdf = EvaluateRayTracingBRDF(material, V, lightSample.Direction);
    if (!any(brdf.Diffuse + brdf.Specular > 0.0) || !any(lightSample.Radiance > 0.0))
    {
        return result;
    }
    result.HitDistance = lightSample.IsInfinite ? settings.MaxDistance : lightSample.Distance;
    if (!TraceSampledLightVisibility(surface, lightSample, settings, shadowOriginOffset))
    {
        return result;
    }

    const float lightPdf = selectionPmf * lightSample.Pdf;
    float brdfPdf = 0.0;
    // A competing technique needs both an allocated sample and support for the
    // same emitter event. Continuous BRDF sampling cannot generate delta lights.
    if (sampleCounts.BRDFSampleCount > 0u && lightSample.SupportsBSDFSampling && !lightSample.IsDelta)
    {
        brdfPdf = EvaluateRayTracingBRDFPdf(material, V, lightSample.Direction);
    }
    const float weight = MISPowerHeuristic(
        float(sampleCounts.LightSampleCount) * lightPdf,
        float(sampleCounts.BRDFSampleCount) * brdfPdf);
    const float3 incident = lightSample.Radiance * (weight / lightPdf);
    result.Diffuse = brdf.Diffuse * incident;
    result.Specular = brdf.Specular * incident;
    return result;
}

// Existing integrators keep their uncorrected visibility policy. Only callers
// supplying a displacement opt into smooth-surface shadow-origin correction.
DirectLightingSample EvaluateDirectLightingSample(GeometrySurface surface, RayTracingMaterial material,
    float3 V, LightSamplingContext context, StructuredBuffer<float> weightTree, uint leafBase,
    float4 randomSample, SamplerState materialSampler, RayTracingLightingSettings settings,
    LightingSampleCounts sampleCounts)
{
    return EvaluateDirectLightingSample(surface, material, V, context, weightTree, leafBase, randomSample,
        materialSampler, settings, sampleCounts, float3(0.0, 0.0, 0.0));
}

// Complete response for reference/path integrators.
float3 EvaluateDirectLighting(GeometrySurface surface, RayTracingMaterial material, float3 V,
    LightSamplingContext context, StructuredBuffer<float> weightTree, uint leafBase,
    float4 randomSample, SamplerState materialSampler, RayTracingLightingSettings settings,
    LightingSampleCounts sampleCounts)
{
    const DirectLightingSample result = EvaluateDirectLightingSample(surface, material, V, context, weightTree, leafBase,
        randomSample, materialSampler, settings, sampleCounts);
    return result.Diffuse + result.Specular;
}

// Adapts a ray hit to the light sampler's representation. Unknown/noncandidate
// emitters return zero through EvaluateLightPdf. Integrators need not construct
// LightSampleLocation or know how the sampler stores its lights.
float EvaluateLightPdfAtHit(LightSamplingContext context, RayTracingHit hit,
    StructuredBuffer<float> weightTree, uint leafBase)
{
    const uint lightSlotId = GetInstanceData(hit.InstanceIndex).LightSlotId;
    return EvaluateLightPdf(context, MakeEmissiveLightSampleLocation(hit, lightSlotId), weightTree, leafBase);
}

float EvaluateEmissionMISWeight(LightSamplingContext previousContext, RayTracingHit hit,
    StructuredBuffer<float> weightTree, uint leafBase,
    float previousBRDFPdf, LightingSampleCounts previousSampleCounts)
{
    // A camera hit has no previous BRDF sample. A path with no NEE at the
    // previous vertex has no competing light estimate either.
    if (previousSampleCounts.BRDFSampleCount == 0u || previousSampleCounts.LightSampleCount == 0u)
    {
        return 1.0;
    }
    const float lightPdf = EvaluateLightPdfAtHit(previousContext, hit, weightTree, leafBase);
    return MISPowerHeuristic(
        float(previousSampleCounts.BRDFSampleCount) * previousBRDFPdf,
        float(previousSampleCounts.LightSampleCount) * lightPdf);
}

#endif // SPARK_LIB_RAY_TRACING_LIGHTING_HLSLI
