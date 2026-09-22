// Indirect diffuse. Full-screen triangle that adds the environment's irradiance to
// SceneColor, or a constant ambient when no environment is baked. This is the pass DDGI
// or SSGI replaces: the signal stays, its source changes.
//
// View independent, so no world position is reconstructed. Sky pixels are culled by the
// rasterizer (far-plane triangle, depth-test Less against SceneDepth).

#include <Shaders/ViewBindings.hlsli>
#include <Shaders/SceneBindings.hlsli>
#include <Shaders/Lib/DeferredShadingCommon.hlsli>
#include <Shaders/Lib/BRDF/Diffuse.hlsli>

// No depth: irradiance is view independent, so there is no world position to reconstruct.
// SceneDepth is still bound as the read-only depth-stencil attachment that culls the sky.
Texture2D g_GBufferNormal    : register(t0, space2);
Texture2D g_GBufferSurface   : register(t1, space2);
Texture2D g_GBufferBaseColor : register(t2, space2);

// Used when no environment is bound (no skybox, or its bake is still uploading).
static const float3 g_Ambient = float3(0.03, 0.03, 0.03);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv       = uv;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    GBufferData gbuffer = DecodeGBufferData(
        g_GBufferNormal, g_GBufferSurface, g_GBufferBaseColor, int2(input.position.xy));

    float3 N = normalize(gbuffer.WorldNormal);

    float3 color;
    if (HasEnvironmentIBL())
    {
        // The cube stores E, not E/pi -- every 1/pi lives inside the BRDF library. DROPPING
        // Fd_Lambert() MAKES DIFFUSE pi TIMES TOO BRIGHT, which after tonemapping reads as
        // "slightly bright" and is effectively impossible to spot by eye.
        float3 irradiance = g_IrradianceCube.SampleLevel(g_IBLSampler, N, 0.0).rgb;
        // g_EnvIntensity also scales the visible sky (Skybox.hlsl), so the two stay in step.
        color = gbuffer.DiffuseColor * Fd_Lambert() * irradiance * g_EnvIntensity;
    }
    else
    {
        color = g_Ambient * gbuffer.BaseColor;
    }

    // Only the material's own occlusion map for now; P4's screen-space AO multiplies in here.
    color *= gbuffer.GBufferAO;

    color += SpaceZeroKeepAlive();

    // Alpha is held by the blend state, so what is written here never lands.
    return float4(color * g_PreExposure, 0.0);
}
