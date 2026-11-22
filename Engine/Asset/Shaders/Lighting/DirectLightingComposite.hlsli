#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Lib/ShadingCommon.hlsli>
#include "DirectLightingSignal.hlsli"

Texture2D<float4> g_DirectDiffuse : register(t0, space2);
Texture2D<float4> g_DirectSpecular : register(t1, space2);
Texture2D g_GBufferSurface : register(t2, space2);
Texture2D g_GBufferBaseColor : register(t3, space2);

float4 DirectLightingFullscreenPosition(uint vertexId)
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

float4 CompositeDirectLighting(int2 pixel, float3 diffuse, float3 specular, uint viewIndex)
{
    const float4 surface = g_GBufferSurface.Load(int3(pixel, 0));
    const float3 baseColor = g_GBufferBaseColor.Load(int3(pixel, 0)).rgb;
    float3 diffuseFactor;
    float3 specularFactor;
    DirectLightingMaterialFactors(ComputeDiffuseColor(baseColor, surface.r), ComputeF0(surface.g, baseColor, surface.r),
        diffuseFactor, specularFactor);
    const float3 radiance = diffuse * diffuseFactor + specular * specularFactor;
    // SceneColor already contains sharp emissive. Apply pre-exposure to the added lighting.
    return float4(radiance * GetView(viewIndex).preExposure, 0.0);
}

float4 CompositeRawDirectLighting(int2 pixel, uint viewIndex)
{
    return CompositeDirectLighting(pixel, g_DirectDiffuse.Load(int3(pixel, 0)).rgb,
        g_DirectSpecular.Load(int3(pixel, 0)).rgb, viewIndex);
}
