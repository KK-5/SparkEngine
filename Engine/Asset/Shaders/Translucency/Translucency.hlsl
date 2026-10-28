#include <Shaders/ViewBindings.hlsli>
#include <Shaders/SceneBindings.hlsli>
#include <Shaders/Material/MaterialTemplate.hlsli>
#include <Shaders/VertexFactory/LocalVertexFactory.hlsli>
#include <Shaders/Lib/ShadingCommon.hlsli>
#include <Shaders/Lib/Lights.hlsli>
#include <Shaders/Lib/Shadow/ShadowSampling.hlsli>
#include <Shaders/Lib/BRDF/EnvBRDF.hlsli>
#include <Shaders/Lib/AmbientOcclusion.hlsli>
#include <Shaders/Lib/SpecularAA.hlsli>

struct ScopeParameters
{
    uint viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

// Translucent surfaces, forward, blended over SceneColor by the pass's state: the material lit
// in place, its colour and opacity whatever the blend mode. No velocity: what is behind keeps
// its own.

SamplerState           g_MatSampler    : register(s0, space2);
Texture2DArray         g_ShadowMaps    : register(t0, space2);   // shadow maps as R32_FLOAT, bound once they exist
SamplerComparisonState g_ShadowSampler : register(s1, space2);

struct VSOutput
{
    float4 Position : SV_Position;
    VertexFactoryInterpolantsVSToPS Interpolants;
};

VSOutput VSMain(VertexFactoryInput input)
{
    VertexFactoryIntermediates intermediates = GetVertexFactoryIntermediates(input);

    float4 worldPosition = VertexFactoryGetWorldPosition(input, intermediates);
    MaterialVertexParameters vertexParameters = GetMaterialVertexParameters(input, intermediates, worldPosition.xyz);
    worldPosition.xyz += GetMaterialWorldPositionOffset(vertexParameters);

    VSOutput output;
    output.Position     = mul(GetView(g_Scope.viewIndex).viewProjection, worldPosition);
    output.Interpolants = VertexFactoryGetInterpolantsVSToPS(input, intermediates);
    return output;
}

//! Sampled from the shadow maps at the surface itself: the ShadowMask holds the opaque depth's.
//! Lights with a mask slot are the shadowed ones, as in the Lights pass, and ShadowViewSystem
//! grants none until the shadow maps exist, so they are bound whenever this reaches them.
float LightShadow(LightData light, float3 worldPos, float3 N)
{
    if (light.shadowMaskIndex < 0)
    {
        return 1.0;
    }

    int map = light.shadowIndex;
    if (light.shadowMapCount > 1)
    {
        map += int(CubeFaceIndex(worldPos - light.position));
    }
    return SampleShadow(GetShadowView(map), uint(map), worldPos, N,
                        g_ShadowMaps, g_ShadowSampler, g_ShadowMapTexelSize);
}

//! Every light of the scene. A light grid narrows the loop to the lights of the surface's cell.
float3 GetForwardDirectLighting(float3 worldPos, float3 N, float3 V,
                                float3 diffuseColor, float3 F0, float perceptualRoughness)
{
    float3 color = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < g_LightCount; ++i)
    {
        LightData light = GetLight(i);
        color += EvaluateLightResponse(light, worldPos, N, V, diffuseColor, F0, perceptualRoughness)
               * LightShadow(light, worldPos, N);
    }
    return color;
}

//! The environment, as IndirectDiffuse and Reflections give it to an opaque surface, occluded
//! by the material's own map alone: no screen-space signal holds a translucent surface.
float3 GetForwardIndirectLighting(float3 N, float3 V, float3 baseColor, float3 diffuseColor, float3 F0,
                                  float roughness, float ao)
{
    if (!HasEnvironmentIBL())
    {
        return g_Ambient * baseColor * AOMultiBounce(baseColor, ao);
    }

    // The cube stores E, not E/pi: Fd_Lambert() carries the 1/pi.
    float3 irradiance = g_IrradianceCube.SampleLevel(g_IBLSampler, N, 0.0).rgb;
    float3 diffuse    = diffuseColor * Fd_Lambert() * irradiance * AOMultiBounce(baseColor, ao);

    float  NoV         = max(abs(dot(N, V)), 1e-4);
    float3 R           = reflect(-V, N);
    float  lod         = RoughnessToLod(roughness, g_IBLPrefilteredMipCount);
    float3 prefiltered = g_PrefilteredCube.SampleLevel(g_IBLSampler, R, lod).rgb;
    float3 specular    = prefiltered * SpecularOcclusion(NoV, roughness, ao)
                       * EnvBRDFLut(g_BRDFLut, g_IBLSampler, F0, roughness, NoV);

    return (diffuse + specular) * g_EnvIntensity;
}

float4 PSMain(VSOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target0
{
    const ViewData view = GetView(g_Scope.viewIndex);

    MaterialPixelParameters parameters = GetMaterialPixelParameters(input.Interpolants, input.Position, isFrontFace);

    PixelMaterialInputs inputs;
    CalcMaterialParameters(parameters, inputs, g_MatSampler);

    // As GBuffer.hlsl filters it, on the surface's own derivatives.
    float roughnessA2 = inputs.Roughness * inputs.Roughness;
    roughnessA2 *= roughnessA2;
    float roughness = saturate(sqrt(sqrt(CalculateSpecularAA(parameters.WorldNormal, roughnessA2))));

    float3 worldPos = SvPositionToWorld(view, input.Position);
    float3 N        = parameters.WorldNormal;
    float3 eye      = mul(view.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float3 V        = normalize(eye - worldPos);

    float3 diffuseColor = ComputeDiffuseColor(inputs.BaseColor, inputs.Metallic);
    float3 F0           = ComputeF0(inputs.Specular, inputs.BaseColor, inputs.Metallic);

    // The analytic BRDF clamps as the Lights pass does; the environment takes it unclamped,
    // as the lighting passes do.
    float3 color = GetForwardDirectLighting(worldPos, N, V, diffuseColor, F0, max(roughness, 0.045))
                 + GetForwardIndirectLighting(N, V, inputs.BaseColor, diffuseColor, F0, roughness,
                                              inputs.AmbientOcclusion)
                 + inputs.EmissiveColor;

    return float4(color * view.preExposure, inputs.Opacity);
}
