#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Lib/RayTracing/RayTracingLighting.hlsli>
#include <Shaders/Lib/Sample/SobolBurley.hlsli>

struct ScopeParameters
{
    uint viewIndex;
    uint debugMode;
    uint emissiveInstanceCount;
    uint sampleIndex;
};
#include <Shaders/ScopeBindings.hlsli>

SamplerState g_MaterialSampler : register(s0, space2);
Texture2D<float4> g_History : register(t0, space2); // RGB: linear HDR mean

// Scene-scale offsets for this two-meter Cornell Box. PDFs and BRDFs always use
// the original surface position; only traced rays use the offset origin.
static const float ReferenceRayBias = 1e-4;
static const uint ReferenceMaxBounces = 8u;

LightSamplingContext MakeLightSamplingContext(float3 worldPosition)
{
    LightSamplingContext context;
    context.WorldPosition = worldPosition;
    context.EmissiveInstanceCount = g_Scope.emissiveInstanceCount;
    context.MaterialMipLevel = 0.0;
    context.TwoSidedEmission = true; // matches EvaluateRayTracingMaterial at hits
    return context;
}

float3 TraceReferenceLighting(RayDesc ray, RayTracingHit hit, uint seed, uint maxBounces)
{
    RayTracingLightingSettings raySettings;
    raySettings.RayBias = ReferenceRayBias;
    raySettings.MaxDistance = 1e30;
    raySettings.InstanceMask = 0xFF;

    float3 radiance = 0.0;
    float3 throughput = 1.0;
    LightSamplingContext previousContext = (LightSamplingContext)0;
    LightingSampleCounts previousSampleCounts = (LightingSampleCounts)0;
    float previousBRDFPdf = 0.0;

    [loop]
    for (uint bounce = 0u; bounce < maxBounces; ++bounce)
    {
        const GeometrySurface surface = LoadRayTracingSurface(ray, hit);
        const RayTracingMaterial material = EvaluateRayTracingMaterial(surface, g_MaterialSampler, 0.0);
        const float3 V = -ray.Direction;

        float emissionWeight = 1.0;
        if (any(material.Inputs.EmissiveColor > 0.0))
        {
            emissionWeight = EvaluateEmissionMISWeight(previousContext, hit, previousBRDFPdf, previousSampleCounts);
        }
        radiance += throughput * material.Inputs.EmissiveColor * emissionWeight;

        // The integrator allocates techniques and random dimensions. The public
        // estimator handles their PDFs, capabilities, visibility and MIS weights.
        LightingSampleCounts sampleCounts;
        sampleCounts.LightSampleCount = 1u;
        sampleCounts.BRDFSampleCount = bounce + 1u < maxBounces ? 1u : 0u;
        const LightSamplingContext context = MakeLightSamplingContext(surface.WorldPosition);
        const float4 lightRandomSample = SobolBurley4D(g_Scope.sampleIndex, 2u * bounce, seed);
        radiance += throughput * EvaluateDirectLighting(surface, material, V, context,
            lightRandomSample, g_MaterialSampler, raySettings, sampleCounts);
        if (sampleCounts.BRDFSampleCount == 0u)
        {
            break;
        }

        const float3 randomSample = SobolBurley3D(g_Scope.sampleIndex, 2u * bounce + 1u, seed);
        BRDFSample brdfSample;
        if (!SampleRayTracingBRDF(material, V, randomSample, brdfSample))
        {
            break;
        }
        if (dot(surface.WorldGeometricNormal, brdfSample.Direction) <= 0.0 || !any(brdfSample.Response > 0.0))
        {
            break;
        }
        throughput *= brdfSample.Response / brdfSample.Pdf;
        previousContext = context;
        previousSampleCounts = sampleCounts;
        previousBRDFPdf = brdfSample.Pdf;

        ray.Origin = surface.WorldPosition + surface.WorldGeometricNormal * raySettings.RayBias;
        ray.Direction = brdfSample.Direction;
        ray.TMin = 0.0;
        ray.TMax = raySettings.MaxDistance;
        if (!TraceClosestOpaqueTriangle(ray, raySettings.InstanceMask, hit))
        {
            // Black environment: no artificial ambient or IBL contribution.
            break;
        }
    }
    return radiance;
}

struct VSOutput
{
    float4 Position : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.Position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float3 LinearToDisplay(float3 color)
{
    color = max(color, 0.0);
    return lerp(1.055 * pow(color, 1.0 / 2.4) - 0.055, 12.92 * color, color <= 0.0031308);
}

float3 TracePixel(VSOutput input, ViewData view)
{
    // Fixed primary ray at the pixel center; one new path sample per frame.
    const float2 uv = (input.Position.xy - view.viewRectMin.xy) * view.viewSizeAndInvSize.zw;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float4 nearPoint = mul(view.invViewProj, float4(ndc, 1.0, 1.0));
    RayDesc ray;
    ray.Origin = mul(view.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    ray.Direction = normalize(nearPoint.xyz / nearPoint.w - ray.Origin);
    ray.TMin = 0.001;
    ray.TMax = 20.0;

    RayTracingHit hit;
    if (!TraceClosestOpaqueTriangle(ray, 0xFF, hit))
    {
        // Black environment for lighting; the gray backdrop belongs to debug views.
        return g_Scope.debugMode == 0u || g_Scope.debugMode == 6u
            ? float3(0.0, 0.0, 0.0) : float3(0.02, 0.02, 0.02);
    }
    if (g_Scope.debugMode == 0u || g_Scope.debugMode == 6u)
    {
        const uint2 pixel = uint2(input.Position.xy - view.viewRectMin.xy);
        const uint seed = MakeSobolBurleyPixelSeed(pixel, 0u);
        const uint maxBounces = g_Scope.debugMode == 0u ? 1u : ReferenceMaxBounces;
        return TraceReferenceLighting(ray, hit, seed, maxBounces);
    }
    const RayTracingSurface surface = LoadRayTracingSurface(ray, hit);
    const RayTracingMaterial material = EvaluateRayTracingMaterial(surface, g_MaterialSampler, 0.0);
    float3 color;
    if (g_Scope.debugMode == 1)
    {
        color = LinearToDisplay(material.Inputs.BaseColor);
    }
    else if (g_Scope.debugMode == 2)
    {
        color = material.WorldNormal * 0.5 + 0.5;
    }
    else if (g_Scope.debugMode == 3)
    {
        color = surface.WorldGeometricNormal * 0.5 + 0.5;
    }
    else if (g_Scope.debugMode == 4)
    {
        const float2 texCoord = surface.MaterialParameters.TexCoord0;
        const float checker = fmod(floor(texCoord.x * 8.0) + floor(texCoord.y * 8.0), 2.0);
        color = float3(frac(texCoord), checker);
    }
    else
    {
        color = float3(material.Inputs.Roughness, material.Inputs.Metallic, material.Inputs.Specular);
    }
    return color;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    const ViewData view = GetView(g_Scope.viewIndex);
    const float3 current = TracePixel(input, view);
    // The first sample initializes the mean without reading uninitialized history.
    // Debug colors keep their existing display encoding and bypass accumulation.
    if (g_Scope.sampleIndex == 0u || (g_Scope.debugMode != 0u && g_Scope.debugMode != 6u))
    {
        return float4(current, 1.0);
    }

    const float3 history = g_History.Load(int3(int2(input.Position.xy), 0)).rgb;
    const float sampleCount = float(g_Scope.sampleIndex) + 1.0;
    const float3 mean = history + (current - history) / sampleCount;
    return float4(mean, 1.0);
}
