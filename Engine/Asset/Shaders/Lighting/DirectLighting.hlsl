// One light sample and one pure GGX direction per GBuffer pixel; specular uses MIS.
// No primary ray, indirect lighting, pre-exposure or temporal accumulation.
#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Lib/DeferredShadingCommon.hlsli>
#include <Shaders/Lib/Velocity.hlsli>
#include <Shaders/Lib/RayTracing/RayTracingDirectLighting.hlsli>
#include <Shaders/Lib/Sample/SobolBurley.hlsli>
#include "DirectLightingSignal.hlsli"

struct ScopeParameters
{
    uint viewIndex;
    uint emissiveInstanceCount;
    uint lightingReady;
};
#include <Shaders/ScopeBindings.hlsli>

Texture2D         g_GBufferNormal             : register(t0, space2);
Texture2D         g_GBufferSurface            : register(t1, space2);
Texture2D         g_GBufferBaseColor          : register(t2, space2);
Texture2D         g_Depth                     : register(t3, space2);
Texture2D<float2> g_Velocity                  : register(t4, space2);
Texture2D         g_GBufferGeometricNormal    : register(t5, space2);
Texture2D<float4> g_GBufferShadowOriginOffset : register(t6, space2);
SamplerState      g_MaterialSampler           : register(s0, space2);

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

struct PSOutput
{
    float4 Diffuse         : SV_Target0; // demodulated scene-linear RGB, light-sample distance
    float4 Specular        : SV_Target1; // demodulated scene-linear RGB, GGX hit distance
    float4 NormalRoughness : SV_Target2; // signed world normal, perceptual roughness
    float  ViewZ           : SV_Target3; // positive linear camera depth
    float2 Motion          : SV_Target4; // previous UV - current UV, without jitter
};

PSOutput PSMain(VSOutput input)
{
    PSOutput output = (PSOutput)0;
    output.NormalRoughness = float4(0.0, 0.0, 1.0, 1.0);
    output.ViewZ = 1e30; // outside the denoising range: sky/background

    const int2 pixel = int2(input.Position.xy);
    const ViewData view = GetView(g_Scope.viewIndex);
    const GBufferData gbuffer = GetGBufferData(
        g_GBufferNormal, g_GBufferSurface, g_GBufferBaseColor, g_Depth, pixel);
    if (gbuffer.Depth <= 0.0 || gbuffer.ShadingModelID != SHADINGMODELID_DEFAULT_LIT)
    {
        return output;
    }

    const float3 worldPosition = SvPositionToWorld(view, float4(input.Position.xy, gbuffer.Depth, 1.0));
    const float3 N = normalize(gbuffer.WorldNormal);
    // Match the roughness actually used by BRDF evaluation and GGX sampling.
    output.NormalRoughness = float4(N, max(saturate(gbuffer.Roughness), BRDF_MIN_PERCEPTUAL_ROUGHNESS));
    output.ViewZ = abs(ConvertFromDeviceZ(view, gbuffer.Depth));
    const float2 velocity = g_Velocity.Load(int3(pixel, 0));
    output.Motion = IsVelocityWritten(velocity) ? -velocity * float2(0.5, -0.5) : float2(2.0, 2.0);
    if (g_Scope.lightingReady == 0u)
    {
        return output;
    }

    GeometrySurface surface = (GeometrySurface)0;
    surface.WorldPosition = worldPosition;
    surface.WorldGeometricNormal = normalize(DecodeNormal(g_GBufferGeometricNormal.Load(int3(pixel, 0)).xyz));
    RayTracingMaterial material = (RayTracingMaterial)0;
    material.WorldNormal = N;
    material.Inputs.Roughness = gbuffer.Roughness;
    material.DiffuseColor = gbuffer.DiffuseColor;
    material.SpecularColor = gbuffer.SpecularColor;
    const float3 eye = mul(view.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    const float3 V = normalize(eye - worldPosition);

    LightSamplingContext context;
    context.WorldPosition = worldPosition;
    context.EmissiveInstanceCount = g_Scope.emissiveInstanceCount;
    context.MaterialMipLevel = 0.0;
    context.TwoSidedEmission = true; // matches the existing ray-hit emission policy
    RayTracingLightingSettings settings;
    settings.RayBias = max(1e-4, max(abs(worldPosition.x), max(abs(worldPosition.y), abs(worldPosition.z))) * 2e-6);
    settings.MaxDistance = 1e30;
    settings.InstanceMask = 0xFF;
    const uint2 localPixel = uint2(input.Position.xy - view.viewRectMin.xy);
    const uint seed = MakeSobolBurleyPixelSeed(localPixel, g_Scope.viewIndex);
    // Keep the existing light sequence. GGX uses a separate dimension set, not
    // components reused from light selection/shape; seeds stay fixed per pixel.
    const float4 lightRandomSample = SobolBurley4D(view.frameNumber, 0u, seed);
    const float2 ggxRandomSample = SobolBurley2D(view.frameNumber, 1u, seed);
    const float3 shadowOriginOffset = g_GBufferShadowOriginOffset.Load(int3(pixel, 0)).xyz;
    const DirectLightingEstimate lighting = EvaluateDirectLightingMIS(surface, material, V, context,
        lightRandomSample, ggxRandomSample, g_MaterialSampler, settings, shadowOriginOffset);

    float3 diffuseFactor;
    float3 specularFactor;
    DirectLightingMaterialFactors(gbuffer.DiffuseColor, gbuffer.SpecularColor, diffuseFactor, specularFactor);
    // Distances are raw world-space guides: no PDF/MIS weighting, averaging or
    // material factor. Preserve zero for invalid samples and cap miss/infinity.
    const float diffuseHitDistance = min(lighting.LightSampleDistance, 65504.0);
    const float specularHitDistance = min(lighting.GGXHitDistance, 65504.0);
    output.Diffuse = float4(lighting.Diffuse / diffuseFactor, diffuseHitDistance);
    output.Specular = float4(lighting.Specular / specularFactor, specularHitDistance);
    return output;
}
