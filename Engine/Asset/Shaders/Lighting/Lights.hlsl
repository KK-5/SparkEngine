// Direct lighting. Full-screen triangle that decodes the GBuffer and accumulates every
// analytic light's Cook-Torrance response, attenuated by ShadowMask, into SceneColor.
//
// Sky pixels are culled by the rasterizer rather than a discard: the triangle sits at the
// far plane (z=0, reversed-Z) and depth-tests Less against SceneDepth, so only pixels with
// geometry survive.

#include <Shaders/ViewBindings.hlsli>
#include <Shaders/SceneBindings.hlsli>
#include <Shaders/Lib/DeferredShadingCommon.hlsli>
#include <Shaders/Lib/BRDF/BRDF.hlsli>
#include <Shaders/Lib/Lights.hlsli>
#include <Shaders/Lib/Shadow/ShadowMask.hlsli>

struct ScopeParameters
{
    uint viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

Texture2D      g_GBufferNormal    : register(t0, space2);
Texture2D      g_GBufferSurface   : register(t1, space2);
Texture2D      g_GBufferBaseColor : register(t2, space2);
Texture2D      g_Depth            : register(t3, space2);   // SceneDepth, viewed as R32_FLOAT
Texture2DArray g_ShadowMask       : register(t4, space2);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;   // ndc.xy = uv * 2 - 1
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv       = uv;
    return output;
}

float3 ReconstructWorldPos(ViewData view, float2 uv, float depth)
{
    float2 ndc = uv * 2.0 - 1.0;
    float4 worldH = mul(view.invViewProj, float4(ndc, depth, 1.0));
    return worldH.xyz / worldH.w;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    const ViewData view = GetView(g_Scope.viewIndex);

    int2 px = int2(input.position.xy);
    GBufferData gbuffer = GetGBufferData(
        g_GBufferNormal, g_GBufferSurface, g_GBufferBaseColor, g_Depth, px);

    float3 worldPos = ReconstructWorldPos(view, input.uv, gbuffer.Depth);

    // Clamped so the specular V term can't divide by zero on smooth surfaces at grazing
    // angles.
    float perceptualRoughness = max(gbuffer.Roughness, 0.045);

    float3 N = normalize(gbuffer.WorldNormal);
    float3 eye = mul(view.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float3 V = normalize(eye - worldPos);

    float3 color = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < g_LightCount; ++i)
    {
        LightData light = GetLight(i);
        float shadow = 1.0;
        if (light.shadowMaskIndex >= 0)
        {
            shadow = SampleShadowMask(g_ShadowMask, px, light.shadowMaskIndex);
        }

        if (light.type == 3)
        {
            if (HasAreaLightLut())
            {
                color += EvaluateRectLight(light, worldPos, N, V, gbuffer.DiffuseColor,
                                           gbuffer.SpecularColor, perceptualRoughness,
                                           g_LTC1, g_LTC2, g_IBLSampler) * shadow;
            }
            continue;
        }

        float3 L;
        float3 radiance = EvaluateLight(light, worldPos, L) * shadow;
        color += EvaluateBRDF(N, V, L, gbuffer.DiffuseColor, gbuffer.SpecularColor,
                              perceptualRoughness) * radiance;
    }

    color += SpaceZeroKeepAlive();

    // Alpha is held by the blend state, so what is written here never lands.
    return float4(color * view.preExposure, 0.0);
}
