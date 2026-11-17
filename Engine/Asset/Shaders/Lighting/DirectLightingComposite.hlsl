#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Lib/ShadingCommon.hlsli>
#include "DirectLightingSignal.hlsli"

struct ScopeParameters
{
    uint viewIndex;
};
#include <Shaders/ScopeBindings.hlsli>

Texture2D<float4> g_DirectDiffuse : register(t0, space2);
Texture2D<float4> g_DirectSpecular : register(t1, space2);
Texture2D g_GBufferSurface : register(t2, space2);
Texture2D g_GBufferBaseColor : register(t3, space2);

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

float4 PSMain(VSOutput input) : SV_Target0
{
    const int2 pixel = int2(input.Position.xy);
    // Composition only restores material colors. A full GBuffer decode would
    // leave its unused normal texture absent from the compiled shader inputs.
    const float4 surface = g_GBufferSurface.Load(int3(pixel, 0));
    const float3 baseColor = g_GBufferBaseColor.Load(int3(pixel, 0)).rgb;
    float3 diffuseFactor;
    float3 specularFactor;
    DirectLightingMaterialFactors(ComputeDiffuseColor(baseColor, surface.r), ComputeF0(surface.g, baseColor, surface.r), diffuseFactor, specularFactor);
    const float3 radiance = g_DirectDiffuse.Load(int3(pixel, 0)).rgb * diffuseFactor
                         + g_DirectSpecular.Load(int3(pixel, 0)).rgb * specularFactor;
    // SceneColor already contains sharp emissive. Only the composition applies
    // pre-exposure; future denoising stays between the producer and this pass.
    return float4(radiance * GetView(g_Scope.viewIndex).preExposure, 0.0);
}
