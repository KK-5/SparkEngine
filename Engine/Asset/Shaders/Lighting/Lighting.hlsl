// Deferred lighting. Full-screen triangle that decodes the GBuffer and shades every scene
// light with a Cook-Torrance BRDF, blending into SceneColor.
//
// Sky pixels are culled by the rasterizer rather than a discard: the triangle sits at the
// far plane (z=0, reversed-Z) and depth-tests Less against SceneDepth, so only pixels with
// geometry survive. Sky keeps whatever GBufferPass left in SceneColor, for the skybox.

#include <Shaders/ViewBindings.hlsli>
#include <Shaders/SceneBindings.hlsli>
#include <Shaders/Lib/DeferredShadingCommon.hlsli>
#include <Shaders/Lib/BRDF/BRDF.hlsli>
#include <Shaders/Lib/BRDF/EnvBRDF.hlsli>
#include <Shaders/Lib/Lights.hlsli>

// Per-pass tier. t4 is free; t5 / s0 belong to the shadow atlas Lib/Lights.hlsli declares.
Texture2D g_GBufferNormal    : register(t0, space2);
Texture2D g_GBufferSurface   : register(t1, space2);
Texture2D g_GBufferBaseColor : register(t2, space2);
Texture2D g_Depth            : register(t3, space2);   // SceneDepth, viewed as R32_FLOAT

// Used when no environment is bound (no skybox, or its bake is still uploading).
static const float3 g_Ambient = float3(0.03, 0.03, 0.03);

float3 EvaluateIBL(float3 N, float3 V, float3 diffuseColor, float3 F0,
                   float perceptualRoughness, float ao)
{
    float NoV = max(abs(dot(N, V)), 1e-4);

    // The cube stores E, not E/pi -- every 1/pi lives inside the BRDF library. DROPPING
    // Fd_Lambert() MAKES DIFFUSE pi TIMES TOO BRIGHT, which after tonemapping reads as
    // "slightly bright" and is effectively impossible to spot by eye.
    float3 irradiance = g_IrradianceCube.SampleLevel(g_IBLSampler, N, 0.0).rgb;
    float3 diffuse    = diffuseColor * Fd_Lambert() * irradiance;

    // Explicit LOD: the mips are a roughness axis, not a detail chain.
    float3 R   = reflect(-V, N);
    float  lod = RoughnessToLod(perceptualRoughness, g_IBLPrefilteredMipCount);
    float3 prefiltered = g_PrefilteredCube.SampleLevel(g_IBLSampler, R, lod).rgb;
    float3 specular    = prefiltered * EnvBRDFLut(g_BRDFLut, g_IBLSampler,
                                                  F0, perceptualRoughness, NoV);

    // ao on specular too: strictly that wants a specular-occlusion term, but leaving it
    // unoccluded makes AO vanish entirely on metals.
    return (diffuse + specular) * ao;
}

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

float3 ReconstructWorldPos(float2 uv, float depth)
{
    float2 ndc = uv * 2.0 - 1.0;
    float4 worldH = mul(g_InvViewProj, float4(ndc, depth, 1.0));
    return worldH.xyz / worldH.w;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    GBufferData gbuffer = GetGBufferData(
        g_GBufferNormal, g_GBufferSurface, g_GBufferBaseColor, g_Depth,
        int2(input.position.xy));

    float3 worldPos = ReconstructWorldPos(input.uv, gbuffer.Depth);

    // Clamped so the specular V term can't divide by zero on smooth surfaces at grazing
    // angles. IBL keeps the raw value: it never evaluates that term, and the floor would
    // drag a mirror 0.045*(mipCount-1) off mip 0.
    float perceptualRoughness = max(gbuffer.Roughness, 0.045);
    float iblRoughness        = saturate(gbuffer.Roughness);

    float3 N = normalize(gbuffer.WorldNormal);
    float3 eye = mul(g_InvView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float3 V = normalize(eye - worldPos);

    float3 color = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < g_LightCount; ++i)
    {
        LightData light = GetLight(i);
        float3 L;
        float3 radiance = EvaluateLight(light, worldPos, N, L);
        color += EvaluateBRDF(N, V, L, gbuffer.DiffuseColor, gbuffer.SpecularColor,
                              perceptualRoughness) * radiance;
    }

    // g_EnvIntensity also scales the visible sky (Skybox.hlsl), so the two stay in step.
    if (HasEnvironmentIBL())
    {
        color += EvaluateIBL(N, V, gbuffer.DiffuseColor, gbuffer.SpecularColor,
                             iblRoughness, gbuffer.GBufferAO) * g_EnvIntensity;
    }
    else
    {
        color += g_Ambient * gbuffer.BaseColor * gbuffer.GBufferAO;
    }

    // Alpha is held by the blend state, so what is written here never lands.
    return float4(color * g_PreExposure, 0.0);
}
