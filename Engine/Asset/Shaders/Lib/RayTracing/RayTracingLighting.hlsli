// Direct-light estimation and emitter-hit MIS for the current opaque reflection
// model. Uses engine scene bindings, but has no pass constants or RNG policy.
#ifndef SPARK_LIB_RAY_TRACING_LIGHTING_HLSLI
#define SPARK_LIB_RAY_TRACING_LIGHTING_HLSLI

#include <Shaders/Lib/RayTracing/RayTracingSurface.hlsli>
#include <Shaders/Lib/Sample/LightSampling.hlsli>
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

bool TraceSampledLightVisibility(GeometrySurface surface, LightSample lightSample,
                                 RayTracingLightingSettings settings)
{
    RayDesc shadowRay;
    shadowRay.Origin = surface.WorldPosition + surface.WorldGeometricNormal * settings.RayBias;
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
            return true;
        }
        shadowRay.Direction = toLight / distanceToLight;
        shadowRay.TMax = distanceToLight - settings.RayBias;
    }
    return TraceOpaqueVisibility(shadowRay, settings.InstanceMask);
}

// One next-event-estimation sample. context.WorldPosition must be the receiver's
// original surface position; ray offsets affect visibility only. randomSample is
// supplied by the caller, independently of BRDF samples, and uniform in [0,1)^4.
float3 EvaluateDirectLighting(GeometrySurface surface, RayTracingMaterial material, float3 V,
                              LightSamplingContext context, float4 randomSample,
                              SamplerState materialSampler, RayTracingLightingSettings settings,
                              LightingSampleCounts sampleCounts)
{
    if (sampleCounts.LightSampleCount == 0u)
    {
        return 0.0;
    }
    LightSample lightSample;
    float selectionPmf;
    if (!SampleLight(context, randomSample, materialSampler, lightSample, selectionPmf))
    {
        return 0.0;
    }
    // Opaque reflection cannot transport light through the geometric back side,
    // even when a normal map tilts the shading normal toward that direction.
    if (dot(surface.WorldGeometricNormal, lightSample.Direction) <= 0.0)
    {
        return 0.0;
    }
    const float3 response = EvaluateRayTracingBRDF(material, V, lightSample.Direction);
    if (!any(response > 0.0) || !any(lightSample.Radiance > 0.0))
    {
        return 0.0;
    }
    if (!TraceSampledLightVisibility(surface, lightSample, settings))
    {
        return 0.0;
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
    return response * lightSample.Radiance * (weight / lightPdf);
}

// Adapts a ray hit to the light sampler's representation. Unknown/noncandidate
// emitters return zero through EvaluateLightPdf. Integrators need not construct
// LightReference/LightSampleLocation or know how the selection list is stored.
float EvaluateLightPdfAtHit(LightSamplingContext context, RayTracingHit hit)
{
    LightSampleLocation location = (LightSampleLocation)0;
    location.Light.Source = LIGHT_SOURCE_EMISSIVE_INSTANCE;
    location.Light.Index = hit.InstanceIndex;
    location.PrimitiveIndex = hit.PrimitiveIndex;
    location.Coordinates = hit.Barycentrics;
    return EvaluateLightPdf(context, location);
}

float EvaluateEmissionMISWeight(LightSamplingContext previousContext, RayTracingHit hit,
                                float previousBRDFPdf, LightingSampleCounts previousSampleCounts)
{
    // A camera hit has no previous BRDF sample. A path with no NEE at the
    // previous vertex has no competing light estimate either.
    if (previousSampleCounts.BRDFSampleCount == 0u || previousSampleCounts.LightSampleCount == 0u)
    {
        return 1.0;
    }
    const float lightPdf = EvaluateLightPdfAtHit(previousContext, hit);
    return MISPowerHeuristic(
        float(previousSampleCounts.BRDFSampleCount) * previousBRDFPdf,
        float(previousSampleCounts.LightSampleCount) * lightPdf);
}

#endif // SPARK_LIB_RAY_TRACING_LIGHTING_HLSLI
