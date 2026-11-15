#ifndef SPARK_LIB_LIGHT_SAMPLING_HLSLI
#define SPARK_LIB_LIGHT_SAMPLING_HLSLI

#include <Shaders/SceneBindings.hlsli>
#include <Shaders/Lib/LightRadiance.hlsli>
#include <Shaders/Lib/Geometry/GeometrySurface.hlsli>
#include <Shaders/Material/MaterialTemplate.hlsli>
#include <Shaders/Lib/Sample/TriangleSampling.hlsli>

static const uint LIGHT_SOURCE_SCENE = 0u;
static const uint LIGHT_SOURCE_EMISSIVE_INSTANCE = 1u;
static const float LIGHT_INFINITE_DISTANCE = 3.402823466e+38;

struct LightReference
{
    uint Source;
    // SCENE: g_Lights index. EMISSIVE_INSTANCE: actual g_Instances slot,
    // NOT the index into g_EmissiveInstanceIDs. References belong to this frame.
    uint Index;
};

// Receiver-independent coordinates: reconstruct the same light sample at another
// receiver without generating new random numbers. This is not a ReSTIR reservoir.
struct LightSampleLocation
{
    LightReference Light;
    uint PrimitiveIndex; // emissive mesh only
    // Mesh: barycentric weights 1,2. Rect: uniform square coordinates [0,1)^2.
    // Point/spot/directional: unused.
    float2 Coordinates;
};

struct LightSamplingContext
{
    float3 WorldPosition; // receiver position, before any shadow-ray origin offset
    uint EmissiveInstanceCount; // active count, supplied by the pass; not buffer capacity
    float MaterialMipLevel;
    // Explicit policy: GPU MaterialData currently has no double-sided state.
    // Use true to match the current opaque, two-sided ray-hit emission evaluation.
    // Single-sided mode uses the geometric normal oriented toward authored normals;
    // a future integrator must use the same emission-side policy at BSDF hits.
    bool TwoSidedEmission;
};

struct LightSample
{
    LightSampleLocation Location;
    float3 Position; // undefined for infinite lights
    float3 Normal; // emitter geometric normal; zero for delta lights
    float3 Direction; // unit vector from receiver to emitter
    float Distance;
    float3 Radiance; // unoccluded; no BRDF or receiver cosine included
    // Conditional on selecting Location.Light. Area lights: solid-angle density,
    // including triangle selection for a mesh. Delta lights: discrete mass 1.
    // Multiply by the light-selection PMF exactly once in the estimator.
    float Pdf;
    bool IsDelta;
    bool IsInfinite;
    // Whether the renderer's scattering-ray emitter evaluation supports this
    // representation (on a hit or miss). This is a capability, not visibility.
    bool SupportsBSDFSampling;
};

float UniformLightSelectionPmf(uint emissiveInstanceCount)
{
    const uint count = g_LightCount + emissiveInstanceCount;
    return count > 0u ? 1.0 / float(count) : 0.0;
}

// Selection-PMF query for an identified emitter, e.g. a BRDF ray's hit. A live
// instance is not necessarily in the emissive candidate list. The reference
// integrator can use this linear lookup; sampling itself needs no search.
float EvaluateUniformLightSelectionPmf(LightReference lightReference, uint emissiveInstanceCount)
{
    const float pmf = UniformLightSelectionPmf(emissiveInstanceCount);
    if (lightReference.Source == LIGHT_SOURCE_SCENE)
    {
        return lightReference.Index < g_LightCount ? pmf : 0.0;
    }
    if (lightReference.Source == LIGHT_SOURCE_EMISSIVE_INSTANCE)
    {
        [loop]
        for (uint i = 0u; i < emissiveInstanceCount; ++i)
        {
            if (g_EmissiveInstanceIDs[i] == lightReference.Index)
            {
                return pmf;
            }
        }
    }
    return 0.0;
}

// Logical concatenation of existing lists; no extra GPU light list is required.
// All random inputs in this file must be uniform in [0,1).
bool SelectUniformLight(uint emissiveInstanceCount, float selectionSample,
                        out LightReference lightReference, out float selectionPmf)
{
    lightReference = (LightReference)0;
    selectionPmf = UniformLightSelectionPmf(emissiveInstanceCount);
    const uint count = g_LightCount + emissiveInstanceCount;
    if (count == 0u)
    {
        return false;
    }

    const uint selected = min(uint(selectionSample * float(count)), count - 1u);
    if (selected < g_LightCount)
    {
        lightReference.Source = LIGHT_SOURCE_SCENE;
        lightReference.Index = selected;
    }
    else
    {
        lightReference.Source = LIGHT_SOURCE_EMISSIVE_INSTANCE;
        lightReference.Index = g_EmissiveInstanceIDs[selected - g_LightCount];
    }
    return true;
}

// Reconstructs geometry and conditional PDF without texture fetches or visibility.
// Emissive references must identify a live, geometry-ready instance this frame.
// Coordinates must belong to the referenced triangle/rect.
bool EvaluateLightSampleGeometry(LightSamplingContext context, LightSampleLocation location,
                                 out LightSample lightSample, out MaterialPixelParameters emitterParameters)
{
    lightSample = (LightSample)0;
    lightSample.Location = location;
    emitterParameters = (MaterialPixelParameters)0;
    float areaPdf = 0.0;

    if (location.Light.Source == LIGHT_SOURCE_SCENE)
    {
        if (location.Light.Index >= g_LightCount)
        {
            return false;
        }
        const LightData light = GetLight(location.Light.Index);
        if (light.type == 0u)
        {
            if (dot(light.direction, light.direction) == 0.0)
            {
                return false;
            }
            lightSample.Direction = DirectionToLight(light, context.WorldPosition);
            lightSample.Distance = LIGHT_INFINITE_DISTANCE;
            lightSample.Pdf = 1.0;
            lightSample.IsDelta = true;
            lightSample.IsInfinite = true;
            return true;
        }
        else if (light.type == 1u || light.type == 2u)
        {
            lightSample.Position = light.position;
            lightSample.Pdf = 1.0;
            lightSample.IsDelta = true;
        }
        else if (light.type == 3u)
        {
            if (light.halfWidth <= 0.0 || light.halfHeight <= 0.0)
            {
                return false;
            }
            const float3 up = cross(light.direction, light.right);
            const float2 offset = location.Coordinates * 2.0 - 1.0;
            lightSample.Position = light.position + offset.x * light.halfWidth * light.right
                + offset.y * light.halfHeight * up;
            lightSample.Normal = light.direction; // engine supplies an orthonormal frame
            areaPdf = 1.0 / (4.0 * light.halfWidth * light.halfHeight);
        }
        else
        {
            return false;
        }
    }
    else if (location.Light.Source == LIGHT_SOURCE_EMISSIVE_INSTANCE)
    {
        const InstanceData instance = GetInstanceData(location.Light.Index);
        const GeometryData geometry = g_Geometries[instance.GeometryIndex];
        const uint triangleCount = GetMeshTriangleCount(geometry);
        if (location.PrimitiveIndex >= triangleCount)
        {
            return false;
        }
        const MeshTriangle meshTriangle = LoadMeshTriangle(geometry, location.PrimitiveIndex);
        const WorldTriangle world = TransformMeshTriangle(instance, meshTriangle);
        if (world.Area <= 0.0)
        {
            return false;
        }
        const GeometrySurface surface = InterpolateGeometrySurface(instance, meshTriangle, location.Coordinates);
        // Mesh emission is evaluated on triangle hits. Analytic scene lights
        // remain false: their current representation has no hit/miss evaluator.
        lightSample.SupportsBSDFSampling = true;
        emitterParameters = surface.MaterialParameters;
        lightSample.Position = surface.WorldPosition;
        // Index winding alone is not the engine's authored front-side convention.
        lightSample.Normal = dot(world.GeometricNormal, emitterParameters.WorldVertexNormal) < 0.0
            ? -world.GeometricNormal : world.GeometricNormal;
        areaPdf = UniformTriangleAreaPdf(world.Area) / float(triangleCount);
    }
    else
    {
        return false;
    }

    const float3 toLight = lightSample.Position - context.WorldPosition;
    const float distanceSquared = dot(toLight, toLight);
    if (distanceSquared <= 0.0)
    {
        return false;
    }
    lightSample.Distance = sqrt(distanceSquared);
    lightSample.Direction = toLight / lightSample.Distance;
    if (!lightSample.IsDelta)
    {
        float emitterCosine = dot(lightSample.Normal, -lightSample.Direction);
        if (location.Light.Source == LIGHT_SOURCE_EMISSIVE_INSTANCE && context.TwoSidedEmission)
        {
            emitterCosine = abs(emitterCosine);
        }
        if (emitterCosine <= 0.0)
        {
            return false;
        }
        // dOmega = cos(thetaLight) / distance^2 * dArea.
        lightSample.Pdf = areaPdf * distanceSquared / emitterCosine;
    }
    return true;
}

bool ReconstructLightSample(LightSamplingContext context, LightSampleLocation location,
                            SamplerState materialSampler, out LightSample lightSample)
{
    MaterialPixelParameters emitterParameters;
    if (!EvaluateLightSampleGeometry(context, location, lightSample, emitterParameters))
    {
        return false;
    }
    if (location.Light.Source == LIGHT_SOURCE_EMISSIVE_INSTANCE)
    {
        // Black texels contribute zero. Do not retry/reject them based on emission:
        // the PDF describes uniform area sampling, not texture importance sampling.
        lightSample.Radiance = EvaluateMaterialEmissionLevel(
            emitterParameters, materialSampler, context.MaterialMipLevel);
    }
    else
    {
        const LightData light = GetLight(location.Light.Index);
        if (light.type == 3u)
        {
            // Area-to-solid-angle conversion already accounts for distance falloff.
            lightSample.Radiance = light.color * light.intensity
                * EvaluateLightRangeWindow(light, context.WorldPosition);
        }
        else
        {
            float3 unusedDirection;
            lightSample.Radiance = EvaluateLight(light, context.WorldPosition, unusedDirection);
        }
    }
    return true;
}

// Conditional density for the light-sampling technique at an identified emitter
// location, useful for MIS at a BSDF hit. Delta directions have no continuous
// solid-angle density: return zero, although SampleLight reports mass 1 for them.
// The caller supplies the selection PMF for this active source separately.
float EvaluateLightSamplePdf(LightSamplingContext context, LightSampleLocation location)
{
    LightSample lightSample;
    MaterialPixelParameters unusedParameters;
    if (!EvaluateLightSampleGeometry(context, location, lightSample, unusedParameters))
    {
        return 0.0;
    }
    return lightSample.IsDelta ? 0.0 : lightSample.Pdf;
}

// Complete continuous light-sampling density at an identified location. Keep
// the selection strategy paired with SampleLight here, rather than in integrators.
float EvaluateLightPdf(LightSamplingContext context, LightSampleLocation location)
{
    const float selectionPmf = EvaluateUniformLightSelectionPmf(location.Light, context.EmissiveInstanceCount);
    if (selectionPmf <= 0.0)
    {
        return 0.0;
    }
    return selectionPmf * EvaluateLightSamplePdf(context, location);
}

// Sampling within a selected light is separate from the choice of light. A future
// importance sampler/ReSTIR candidate generator can reuse this entry point.
bool SampleSelectedLight(LightSamplingContext context, LightReference lightReference,
                         float3 shapeSample, SamplerState materialSampler, out LightSample lightSample)
{
    lightSample = (LightSample)0;
    LightSampleLocation location = (LightSampleLocation)0;
    location.Light = lightReference;
    if (lightReference.Source == LIGHT_SOURCE_EMISSIVE_INSTANCE)
    {
        const InstanceData instance = GetInstanceData(lightReference.Index);
        const uint triangleCount = GetMeshTriangleCount(g_Geometries[instance.GeometryIndex]);
        if (triangleCount == 0u)
        {
            return false;
        }
        location.PrimitiveIndex = min(uint(shapeSample.x * float(triangleCount)), triangleCount - 1u);
        location.Coordinates = SampleUniformTriangle(shapeSample.yz);
    }
    else
    {
        location.Coordinates = shapeSample.yz;
    }
    return ReconstructLightSample(context, location, materialSampler, lightSample);
}

// randomSample.x chooses among scene lights + emissive instances; .y chooses a
// mesh triangle; .zw chooses a surface point (e.g. one SobolBurley4D call).
// false means a zero contribution; do not retry zero-area/backfacing samples or
// the light/triangle selection probabilities would need renormalization.
bool SampleLight(LightSamplingContext context, float4 randomSample, SamplerState materialSampler,
                 out LightSample lightSample, out float selectionPmf)
{
    lightSample = (LightSample)0;
    LightReference lightReference;
    if (!SelectUniformLight(context.EmissiveInstanceCount, randomSample.x, lightReference, selectionPmf))
    {
        return false;
    }
    return SampleSelectedLight(context, lightReference, randomSample.yzw, materialSampler, lightSample);
}

// Visibility, ray offsets, BRDFs and integration belong to the caller. Rect lights
// in g_Lights are not automatically intersectable geometry: only use a competing
// BSDF-hit MIS PDF when that technique can actually hit the same emitter. Do not
// register one physical emitter as both a rect light and an emissive mesh.
// Environment sampling needs a radiance environment input/distribution; the scene's
// prefiltered IBL cubes are not such an input and are not sampled here.

#endif // SPARK_LIB_LIGHT_SAMPLING_HLSLI
