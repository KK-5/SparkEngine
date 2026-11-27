#ifndef SPARK_LIB_LIGHT_SAMPLING_HLSLI
#define SPARK_LIB_LIGHT_SAMPLING_HLSLI

#include <Shaders/SceneBindings.hlsli>
#include <Shaders/Lib/BRDF/Constants.hlsli>
#include <Shaders/Lib/LightRadiance.hlsli>
#include <Shaders/Lib/Geometry/GeometrySurface.hlsli>
#include <Shaders/Material/MaterialTemplate.hlsli>
#include <Shaders/Lib/Sample/TriangleSampling.hlsli>

static const float LIGHT_INFINITE_DISTANCE = 3.402823466e+38;

// Receiver-independent coordinates: reconstruct the same light sample at another
// receiver without generating new random numbers. This is not a ReSTIR reservoir.
struct LightSampleLocation
{
    uint LightSlotId;
    uint PrimitiveIndex; // emissive mesh only
    // Mesh: barycentric weights 1,2. Rect / directional cone: uniform coordinates [0,1)^2.
    // Delta lights: unused.
    float2 Coordinates;
};

struct LightSamplingContext
{
    float3 WorldPosition; // receiver position, before any shadow-ray origin offset
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
    uint Type; // LIGHT_TYPE_*
    float3 Position; // undefined for infinite lights
    float3 Normal; // emitter geometric normal; zero for delta lights
    float3 Direction; // unit vector from receiver to emitter
    float Distance;
    float3 Radiance; // unoccluded; no BRDF or receiver cosine included
    // Conditional on selecting Location.LightSlotId. Extended lights: solid-angle density,
    // including triangle selection for a mesh. Delta lights: discrete mass 1.
    // Multiply by the light-selection PMF exactly once in the estimator.
    float Pdf;
    bool IsDelta;
    bool IsInfinite;
    // Whether the renderer's scattering-ray emitter evaluation supports this
    // representation (on a hit or miss). This is a capability, not visibility.
    bool SupportsBSDFSampling;
    // Scene-light authoring switch. Emissive mesh samples always cast shadows.
    bool CastShadow;
};

float GetLightWeightTotal(StructuredBuffer<float> weightTree, uint leafBase)
{
    // A zero leaf base means there is no tree to read.
    [branch]
    if (leafBase == 0u)
    {
        return 0.0;
    }
    return weightTree[1];
}

// All random inputs in this file must be uniform in [0,1).
bool SelectLight(StructuredBuffer<float> weightTree, uint leafBase,
                 float selectionSample, out uint lightSlotId, out float selectionPmf)
{
    const float totalWeight = GetLightWeightTotal(weightTree, leafBase);
    if (!(totalWeight > 0.0))
    {
        lightSlotId = INVALID_LIGHT_SLOT;
        selectionPmf = 0.0;
        return false;
    }

    float target = selectionSample * totalWeight;
    uint node = 1u;
    [loop]
    while (node < leafBase)
    {
        const uint leftNode = 2u * node;
        const float leftWeight = weightTree[leftNode];
        node = leftNode;
        // A rounded upper boundary must stay in the nonempty subtree, including
        // the last level where the right leaf can be padding.
        if (target >= leftWeight && weightTree[leftNode + 1u] > 0.0)
        {
            target -= leftWeight;
            node = leftNode + 1u;
        }
    }

    lightSlotId = node - leafBase;
    selectionPmf = weightTree[node] / totalWeight;
    return true;
}

bool FinalizeFiniteLightSample(LightSamplingContext context, float areaPdf, inout LightSample lightSample)
{
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
        if (lightSample.Type == LIGHT_TYPE_EMISSIVE_MESH && context.TwoSidedEmission)
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

bool EvaluateAnalyticSampleGeometry(LightSamplingContext context, LightSampleLocation location,
    AnalyticLightData light, out LightSample lightSample)
{
    lightSample = (LightSample)0;
    lightSample.Location = location;
    lightSample.Type = light.type;
    lightSample.CastShadow = light.castShadow != 0u;
    float areaPdf = 0.0;
    if (light.type == LIGHT_TYPE_DIRECTIONAL)
    {
        if (dot(light.direction, light.direction) == 0.0)
        {
            return false;
        }
        lightSample.Direction = DirectionToLight(light, context.WorldPosition);
        lightSample.Distance = LIGHT_INFINITE_DISTANCE;
        lightSample.Pdf = 1.0;
        // A numerically negligible cone can use the delta limit.
        lightSample.IsDelta = light.directionalConeHeight <= 1e-12;
        lightSample.IsInfinite = true;
        if (!lightSample.IsDelta)
        {
            // Uniform in solid angle: sample cos(theta) uniformly over the cap.
            const float oneMinusCosTheta = location.Coordinates.x * light.directionalConeHeight;
            const float sinTheta = sqrt(oneMinusCosTheta * (2.0 - oneMinusCosTheta));
            float sinPhi, cosPhi;
            sincos(2.0 * PI * location.Coordinates.y, sinPhi, cosPhi);
            const float3 axis = lightSample.Direction;
            const float3 up = cross(axis, light.right);
            lightSample.Direction = normalize(axis * (1.0 - oneMinusCosTheta)
                + (light.right * cosPhi + up * sinPhi) * sinTheta);
            lightSample.Pdf = rcp(2.0 * PI * light.directionalConeHeight);
        }
        return true;
    }
    if (light.type == LIGHT_TYPE_POINT || light.type == LIGHT_TYPE_SPOT)
    {
        lightSample.Position = light.position;
        lightSample.Pdf = 1.0;
        lightSample.IsDelta = true;
    }
    else if (light.type == LIGHT_TYPE_RECT)
    {
        if (light.halfWidth <= 0.0 || light.halfHeight <= 0.0)
        {
            return false;
        }
        const float3 up = cross(light.direction, light.right);
        const float2 offset = location.Coordinates * 2.0 - 1.0;
        lightSample.Position = light.position + offset.x * light.halfWidth * light.right
            + offset.y * light.halfHeight * up;
        lightSample.Normal = light.direction;
        areaPdf = 1.0 / (4.0 * light.halfWidth * light.halfHeight);
    }
    else
    {
        return false;
    }
    return FinalizeFiniteLightSample(context, areaPdf, lightSample);
}

bool EvaluateEmissiveSampleGeometry(LightSamplingContext context, LightSampleLocation location,
    EmissiveLightData light, out LightSample lightSample, out float2 texCoord)
{
    lightSample = (LightSample)0;
    lightSample.Location = location;
    lightSample.Type = LIGHT_TYPE_EMISSIVE_MESH;
    lightSample.CastShadow = true;
    lightSample.SupportsBSDFSampling = true;
    texCoord = 0.0;
    const uint triangleCount = light.geometry.TriangleCount;
    if (location.PrimitiveIndex >= triangleCount)
    {
        return false;
    }
    const MeshTriangle meshTriangle = LoadMeshTriangle(light.geometry, location.PrimitiveIndex);
    const WorldTriangle world = TransformMeshTriangle(light.model, meshTriangle);
    if (world.Area <= 0.0)
    {
        return false;
    }
    const float3 weights = GetTriangleBarycentricWeights(location.Coordinates);
    texCoord = meshTriangle.Vertex0.TexCoord0 * weights.x + meshTriangle.Vertex1.TexCoord0 * weights.y
        + meshTriangle.Vertex2.TexCoord0 * weights.z;
    lightSample.Position = world.Position0 + world.Edge1 * weights.y + world.Edge2 * weights.z;
    lightSample.Normal = world.GeometricNormal;

    // Transform the authored normal by the inverse-transpose up to a positive scale.
    const float3 localNormal = meshTriangle.Vertex0.Normal * weights.x + meshTriangle.Vertex1.Normal * weights.y
        + meshTriangle.Vertex2.Normal * weights.z;
    const float3x3 model = (float3x3)light.model;
    const float3 cof0 = cross(model[1], model[2]);
    const float3 cof1 = cross(model[2], model[0]);
    const float3 cof2 = cross(model[0], model[1]);
    const float orientation = dot(model[0], cof0) < 0.0 ? -1.0 : 1.0;
    const float3 worldNormal = orientation * mul(float3x3(cof0, cof1, cof2), localNormal);
    if (dot(lightSample.Normal, worldNormal) < 0.0)
    {
        lightSample.Normal = -lightSample.Normal;
    }
    const float areaPdf = UniformTriangleAreaPdf(world.Area) / float(triangleCount);
    return FinalizeFiniteLightSample(context, areaPdf, lightSample);
}

bool ReconstructEmissiveMeshSample(LightSamplingContext context, LightSampleLocation location,
    EmissiveLightData light, SamplerState materialSampler, out LightSample lightSample)
{
    float2 texCoord;
    if (!EvaluateEmissiveSampleGeometry(context, location, light, lightSample, texCoord))
    {
        return false;
    }
    const float3 texel = SampleMaterialTextureLevel(
        light.textureIndex, texCoord, materialSampler, context.MaterialMipLevel).rgb;
    lightSample.Radiance = light.color * light.intensity * texel;
    return true;
}

bool ReconstructLightSample(LightSamplingContext context, LightSampleLocation location, LightData data,
    SamplerState materialSampler, out LightSample lightSample)
{
    if (data.type == LIGHT_TYPE_EMISSIVE_MESH)
    {
        return ReconstructEmissiveMeshSample(context, location, DecodeEmissiveLight(data), materialSampler, lightSample);
    }
    const AnalyticLightData light = DecodeAnalyticLight(data);
    if (!EvaluateAnalyticSampleGeometry(context, location, light, lightSample))
    {
        return false;
    }
    if (light.type == LIGHT_TYPE_RECT)
    {
        // Area-to-solid-angle conversion already accounts for distance falloff.
        lightSample.Radiance = light.color * light.intensity
            * EvaluateLightRangeWindow(light, context.WorldPosition);
    }
    else if (light.type == LIGHT_TYPE_DIRECTIONAL && !lightSample.IsDelta)
    {
        // Preserve irradiance on a surface facing the cone axis as its size varies.
        const float meanCosine = 1.0 - 0.5 * light.directionalConeHeight;
        lightSample.Radiance = light.color * light.intensity * (lightSample.Pdf / meanCosine);
    }
    else
    {
        float3 unusedDirection;
        lightSample.Radiance = EvaluateLight(light, context.WorldPosition, unusedDirection);
    }
    return true;
}

// Conditional density for the light-sampling technique at an identified emitter
// location, useful for MIS at a BSDF hit. Delta directions have no continuous
// solid-angle density: return zero, although SampleLight reports mass 1 for them.
// The caller supplies the selection PMF for this active source separately.
float EvaluateLightSamplePdf(LightSamplingContext context, LightSampleLocation location, LightData data)
{
    LightSample lightSample;
    if (data.type == LIGHT_TYPE_EMISSIVE_MESH)
    {
        float2 unusedTexCoord;
        return EvaluateEmissiveSampleGeometry(context, location, DecodeEmissiveLight(data), lightSample, unusedTexCoord)
            ? lightSample.Pdf : 0.0;
    }
    if (!EvaluateAnalyticSampleGeometry(context, location, DecodeAnalyticLight(data), lightSample))
    {
        return 0.0;
    }
    return lightSample.IsDelta ? 0.0 : lightSample.Pdf;
}

// Complete continuous light-sampling density at an identified location. Keep
// the selection strategy paired with SampleLight here, rather than in integrators.
float EvaluateLightPdf(LightSamplingContext context, LightSampleLocation location,
                       StructuredBuffer<float> weightTree, uint leafBase)
{
    if (location.LightSlotId >= g_LightSlotCount)
    {
        return 0.0;
    }

    const float totalWeight = GetLightWeightTotal(weightTree, leafBase);
    if (!(totalWeight > 0.0))
    {
        return 0.0;
    }

    const float selectionPmf = weightTree[leafBase + location.LightSlotId] / totalWeight;
    if (selectionPmf <= 0.0)
    {
        return 0.0;
    }
    return selectionPmf * EvaluateLightSamplePdf(context, location, GetLight(location.LightSlotId));
}

// Sampling within a selected light is separate from the choice of light. A future
// importance sampler/ReSTIR candidate generator can reuse this entry point.
bool SampleSelectedLight(LightSamplingContext context, uint lightSlotId,
                         float3 shapeSample, SamplerState materialSampler, out LightSample lightSample)
{
    lightSample = (LightSample)0;
    LightSampleLocation location = (LightSampleLocation)0;
    location.LightSlotId = lightSlotId;
    const LightData data = GetLight(lightSlotId);
    if (data.type == LIGHT_TYPE_EMISSIVE_MESH)
    {
        const EmissiveLightData light = DecodeEmissiveLight(data);
        const uint triangleCount = light.geometry.TriangleCount;
        if (triangleCount == 0u)
        {
            return false;
        }
        location.PrimitiveIndex = min(uint(shapeSample.x * float(triangleCount)), triangleCount - 1u);
        location.Coordinates = SampleUniformTriangle(shapeSample.yz);
        return ReconstructEmissiveMeshSample(context, location, light, materialSampler, lightSample);
    }
    location.Coordinates = shapeSample.yz;
    return ReconstructLightSample(context, location, data, materialSampler, lightSample);
}

// randomSample.x chooses among scene lights + emissive instances; .y chooses a
// mesh triangle; .zw chooses a surface point (e.g. one SobolBurley4D call).
// false means a zero contribution; do not retry zero-area/backfacing samples or
// the light/triangle selection probabilities would need renormalization.
bool SampleLight(LightSamplingContext context, StructuredBuffer<float> weightTree, uint leafBase,
                 float4 randomSample, SamplerState materialSampler, out LightSample lightSample, out float selectionPmf)
{
    lightSample = (LightSample)0;
    uint lightSlotId;
    if (!SelectLight(weightTree, leafBase, randomSample.x, lightSlotId, selectionPmf))
    {
        return false;
    }
    return SampleSelectedLight(context, lightSlotId, randomSample.yzw, materialSampler, lightSample);
}

// Visibility, ray offsets, BRDFs and integration belong to the caller. Rect lights
// in g_Lights are not automatically intersectable geometry: only use a competing
// BSDF-hit MIS PDF when that technique can actually hit the same emitter. Do not
// register one physical emitter as both a rect light and an emissive mesh.
// Environment sampling needs a radiance environment input/distribution; the scene's
// prefiltered IBL cubes are not such an input and are not sampled here.

#endif // SPARK_LIB_LIGHT_SAMPLING_HLSLI
