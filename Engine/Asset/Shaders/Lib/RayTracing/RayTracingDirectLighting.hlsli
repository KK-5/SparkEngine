// Real-time direct lighting: one light sample and one pure GGX reflection sample.
// This allocation is separate from the reference integrator's mixed-BRDF strategy.
#ifndef SPARK_LIB_RAY_TRACING_DIRECT_LIGHTING_HLSLI
#define SPARK_LIB_RAY_TRACING_DIRECT_LIGHTING_HLSLI

#include <Shaders/Lib/RayTracing/RayTracingLighting.hlsli>

struct DirectLightingEstimate
{
    float3 Diffuse;
    float3 Specular;
    // World-space distances without denoiser packing.
    float LightSampleDistance; // zero for invalid light samples; retained when occluded
    float SpecularHitDistance; // contributing GGX emitter first, then the contributing light sample
};

// GGX emitter discovery uses this original-receiver ray. Offsetting it would
// change the emitter point/direction and invalidate the PDF evaluated at receiver.
RayDesc MakeDirectLightingGGXRay(GeometrySurface surface, float3 direction,
                                  RayTracingLightingSettings settings)
{
    RayDesc ray;
    ray.Origin = surface.WorldPosition;
    ray.Direction = direction;
    ray.TMin = settings.RayBias;
    ray.TMax = settings.MaxDistance;
    return ray;
}

// Self-intersection exclusion and finite range are part of this estimator's
// GGX technique support. Outside it, only the light technique contributes.
bool SupportsDirectLightingGGXSample(LightSample lightSample, RayTracingLightingSettings settings)
{
    if (lightSample.IsDelta || lightSample.IsInfinite
        || lightSample.Distance < settings.RayBias || lightSample.Distance > settings.MaxDistance)
    {
        return false;
    }
    // Analytic rectangle support is local to this estimator. Do not change the
    // shared capability flag: reference paths do not discover these rectangles.
    if (lightSample.Location.Light.Source == LIGHT_SOURCE_EMISSIVE_INSTANCE)
    {
        return true;
    }
    if (lightSample.Location.Light.Source == LIGHT_SOURCE_SCENE)
    {
        const LightData light = GetLight(lightSample.Location.Light.Index);
        return light.type == 3u;
    }
    return false;
}

// A mesh emitter must also be visible along the original sampling direction:
// GGX discovers only the closest scene triangle on that ray. Apply this same
// condition to mesh specular NEE, then apply corrected visibility to both sides.
// Diffuse keeps the existing corrected-shadow policy; rectangles are enumerated
// independently and must not be clipped by a scene hit or by another rectangle.
bool TraceDirectLightingMeshVisibility(GeometrySurface surface, LightSample lightSample,
                                        RayTracingLightingSettings settings)
{
    RayDesc ray = MakeDirectLightingGGXRay(surface, lightSample.Direction, settings);
    ray.TMax = lightSample.Distance - settings.RayBias;
    if (ray.TMax <= ray.TMin)
    {
        return true;
    }
    return TraceOpaqueVisibility(ray, settings.InstanceMask);
}

DirectLightingSample EvaluateDirectLightingLightSample(GeometrySurface surface, RayTracingMaterial material,
    float3 V, LightSamplingContext context, StructuredBuffer<float> weightTree, uint leafBase,
    float4 randomSample, SamplerState materialSampler, RayTracingLightingSettings settings, float3 shadowOriginOffset)
{
    DirectLightingSample result = (DirectLightingSample)0;
    LightSample lightSample;
    float selectionPmf;
    if (!SampleLight(context, weightTree, leafBase, randomSample, materialSampler, lightSample, selectionPmf))
    {
        return result;
    }
    if (!any(shadowOriginOffset != 0.0) && dot(surface.WorldGeometricNormal, lightSample.Direction) <= 0.0)
    {
        return result;
    }
    const BRDFResult brdf = EvaluateRayTracingBRDF(material, V, lightSample.Direction);
    if (!any(brdf.Diffuse + brdf.Specular > 0.0) || !any(lightSample.Radiance > 0.0))
    {
        return result;
    }
    const float lightPdf = selectionPmf * lightSample.Pdf;
    if (lightPdf <= 0.0)
    {
        return result;
    }
    result.HitDistance = lightSample.IsInfinite ? settings.MaxDistance : lightSample.Distance;
    if (!TraceSampledLightVisibility(surface, lightSample, settings, shadowOriginOffset))
    {
        return result;
    }

    // Diffuse has no competing GGX estimate. BRDFResult already includes NoL.
    const float3 incident = lightSample.Radiance / lightPdf;
    result.Diffuse = brdf.Diffuse * incident;
    if (!any(brdf.Specular > 0.0))
    {
        return result;
    }
    if (lightSample.Location.Light.Source == LIGHT_SOURCE_EMISSIVE_INSTANCE)
    {
        if (!TraceDirectLightingMeshVisibility(surface, lightSample, settings))
        {
            return result;
        }
    }
    float ggxPdf = 0.0;
    if (SupportsDirectLightingGGXSample(lightSample, settings))
    {
        ggxPdf = EvaluateGGXReflectionPdf(material.WorldNormal, V, lightSample.Direction,
            material.Inputs.Roughness);
    }
    const float weight = MISPowerHeuristic(lightPdf, ggxPdf);
    result.Specular = brdf.Specular * lightSample.Radiance * (weight / lightPdf);
    return result;
}

// One sampled direction may see several nonblocking analytic emitters. Every
// emitter uses its own light-selection PMF and conditional solid-angle density.
void AccumulateDirectLightingGGXContribution(inout DirectLightingSample result, float3 specular, float ggxPdf,
    LightSamplingContext context, LightSample lightSample, float emitterDistance,
    StructuredBuffer<float> weightTree, uint leafBase)
{
    const float lightPdf = EvaluateLightPdf(context, lightSample.Location, weightTree, leafBase);
    const float weight = MISPowerHeuristic(ggxPdf, lightPdf);
    const float3 contribution = specular * lightSample.Radiance * (weight / ggxPdf);
    result.Specular += contribution;
    if (any(contribution > 0.0))
    {
        result.HitDistance = result.HitDistance == 0.0 ? emitterDistance : min(result.HitDistance, emitterDistance);
    }
}

// Zero hit distance means this direction found no contributing emitter.
DirectLightingSample EvaluateDirectLightingGGXSample(GeometrySurface surface, RayTracingMaterial material,
    float3 V, LightSamplingContext context, StructuredBuffer<float> weightTree, uint leafBase,
    float2 randomSample, SamplerState materialSampler, RayTracingLightingSettings settings, float3 shadowOriginOffset)
{
    DirectLightingSample result = (DirectLightingSample)0;
    float3 direction;
    float ggxPdf;
    if (!SampleGGXReflection(material.WorldNormal, V, material.Inputs.Roughness,
        randomSample, direction, ggxPdf))
    {
        return result;
    }
    // Keep failed samples as zero contribution, without retries or PDF renormalization.
    if (!any(shadowOriginOffset != 0.0) && dot(surface.WorldGeometricNormal, direction) <= 0.0)
    {
        return result;
    }
    const BRDFResult brdf = EvaluateRayTracingBRDF(material, V, direction);
    const RayDesc ray = MakeDirectLightingGGXRay(surface, direction, settings);
    RayTracingHit hit;
    if (TraceClosestOpaqueTriangle(ray, settings.InstanceMask, hit))
    {
        LightSample meshSample;
        if (any(brdf.Specular > 0.0)
            && ReconstructEmissiveLightSample(context, hit, materialSampler, meshSample))
        {
            // Original-ray discovery already establishes closest-mesh visibility.
            // Re-aim the corrected shadow ray at the same point, matching NEE.
            if (TraceSampledLightVisibility(surface, meshSample, settings, shadowOriginOffset))
            {
                AccumulateDirectLightingGGXContribution(
                    result, brdf.Specular, ggxPdf, context, meshSample, hit.T, weightTree, leafBase);
            }
        }
    }

    if (!any(brdf.Specular > 0.0))
    {
        return result;
    }
    uint nextLightIndex = 0u;
    LightSample rectSample;
    [loop]
    while (QueryNextVisibleRectLight(surface, context, ray, materialSampler,
        settings, shadowOriginOffset, nextLightIndex, rectSample))
    {
        AccumulateDirectLightingGGXContribution(
            result, brdf.Specular, ggxPdf, context, rectSample, rectSample.Distance, weightTree, leafBase);
    }
    return result;
}

// Caller supplies independent uniform random dimensions: [0,1)^4 for light
// selection/shape and [0,1)^2 for pure GGX VNDF. Exactly one sample per technique;
// sum their specular estimates without dividing by two. No diffuse GGX estimate.
// context.WorldPosition must equal surface.WorldPosition before shadow offsets.
// Settings require 0 < RayBias < MaxDistance.
DirectLightingEstimate EvaluateDirectLightingMIS(GeometrySurface surface, RayTracingMaterial material,
    float3 V, LightSamplingContext context, StructuredBuffer<float> weightTree, uint leafBase,
    float4 lightRandomSample, float2 ggxRandomSample, SamplerState materialSampler,
    RayTracingLightingSettings settings, float3 shadowOriginOffset)
{
    const DirectLightingSample lightSample = EvaluateDirectLightingLightSample(surface, material, V, context,
        weightTree, leafBase, lightRandomSample, materialSampler, settings, shadowOriginOffset);
    const DirectLightingSample ggxSample = EvaluateDirectLightingGGXSample(surface, material, V, context,
        weightTree, leafBase, ggxRandomSample, materialSampler, settings, shadowOriginOffset);
    DirectLightingEstimate result;
    result.Diffuse = lightSample.Diffuse;
    result.Specular = lightSample.Specular + ggxSample.Specular;
    result.LightSampleDistance = lightSample.HitDistance;
    result.SpecularHitDistance = ggxSample.HitDistance;
    if (result.SpecularHitDistance == 0.0 && any(lightSample.Specular > 0.0))
    {
        result.SpecularHitDistance = lightSample.HitDistance;
    }
    return result;
}

#endif // SPARK_LIB_RAY_TRACING_DIRECT_LIGHTING_HLSLI
