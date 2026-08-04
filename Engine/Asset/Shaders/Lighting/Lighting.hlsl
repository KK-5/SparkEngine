// Lighting.hlsl — deferred lighting pass. Full-screen triangle that reads the
// GBuffer (Albedo / Normal / ORM) plus SceneDepth per pixel and shades the scene
// lights (g_Lights, per-scene space0) with a Cook-Torrance BRDF, writing SceneColor.
//
// GBuffer color targets are sampled with Load (integer pixel fetch, point sampling)
// — no sampler, no flip — the natural fit for a 1:1 full-res deferred read. World
// position is reconstructed from SceneDepth (sampled as R32_FLOAT via the pass's
// view override) and g_InvViewProj: build the clip-space point (ndc.xy, depth, 1),
// un-project, divide by w. The per-pixel ndc.xy comes from the VS-passed uv, which
// is exactly what the VS used to place SV_Position, so no manual y-flip is needed.
//
// Sky / uncovered pixels are culled by a read-only depth test instead of a shader
// discard: the full-screen triangle sits at the far plane (z=1) and is depth-tested
// Greater against SceneDepth, so only pixels with geometry (depth < 1) survive — the
// rasterizer rejects the rest via early-Z. SceneColor keeps its clear value where
// culled, for the skybox pass to fill afterwards.

#include <Shaders/ViewBindings.hlsl>       // space1: g_InvViewProj, g_InvView
#include <Shaders/SceneBindings.hlsl>      // space0: g_Lights, g_LightCount, environment IBL
#include <Shaders/Lib/BRDF/BRDF.hlsli>     // Cook-Torrance surface response
#include <Shaders/Lib/BRDF/EnvBRDF.hlsli>  // split-sum environment BRDF
#include <Shaders/Lib/Lights.hlsli>        // per-light L + incident radiance

// Per-pass GBuffer SRVs (space2 = per-pass tier), bound by LightingPass's Compile hook.
Texture2D g_Albedo   : register(t0, space2);
Texture2D g_Normal   : register(t1, space2);
Texture2D g_ORM      : register(t2, space2);
Texture2D g_Depth    : register(t3, space2);   // SceneDepth, viewed as R32_FLOAT
Texture2D g_Emissive : register(t4, space2);   // GBuffer HDR emissive, added un-lit

// Fallback for when no environment is bound (no skybox, or its bake is still uploading).
static const float3 g_Ambient = float3(0.03, 0.03, 0.03);

// Image-based ambient: split-sum against the active skybox's baked environment.
float3 EvaluateIBL(float3 N, float3 V, float3 diffuseColor, float3 F0,
                   float perceptualRoughness, float ao)
{
    float NoV = max(abs(dot(N, V)), 1e-4);   // same floor EvaluateBRDF uses

    // The cube stores E, not E/pi -- this engine keeps every 1/pi inside the BRDF library.
    // DROPPING Fd_Lambert() MAKES DIFFUSE pi TIMES TOO BRIGHT, which after tonemapping
    // reads as "slightly bright" and is effectively impossible to spot by eye.
    // Lambert, not Burley: Burley needs a single light's L, which an integrated
    // environment does not have.
    float3 irradiance = g_IrradianceCube.SampleLevel(g_IBLSampler, N, 0.0).rgb;
    float3 diffuse    = diffuseColor * Fd_Lambert() * irradiance;

    // Explicit LOD: the mips are a roughness axis, so leaving mip selection to the
    // hardware would shade every pixel at whatever roughness its derivatives imply.
    float3 R   = reflect(-V, N);
    float  lod = RoughnessToLod(perceptualRoughness, g_IBLPrefilteredMipCount);
    float3 prefiltered = g_PrefilteredCube.SampleLevel(g_IBLSampler, R, lod).rgb;
    float3 specular    = prefiltered * EnvBRDFLut(g_BRDFLut, g_IBLSampler,
                                                 F0, perceptualRoughness, NoV);

    // ao on specular too: strictly that needs a specular-occlusion term, but leaving it
    // unoccluded makes AO vanish entirely on metals.
    return (diffuse + specular) * ao;
}

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;   // [0,1]+ screen param; ndc.xy = uv*2-1
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    // Full-screen triangle from SV_VertexID: NDC (-1,-1),(3,-1),(-1,3).
    float2 uv  = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    
    // z = 1 (far plane): the read-only depth test (func Greater vs SceneDepth) then
    // culls sky/uncovered pixels, where SceneDepth still holds the far clear value.
    output.position = float4(uv * 2.0 - 1.0, 1.0, 1.0);
    output.uv       = uv;
    return output;
}

float3 ReconstructWorldPos(float2 uv, float depth)
{
    // ndc.xy matches the VS placement (position = float4(uv*2-1, ...)), depth is the
    // stored NDC z, so (ndc, depth, 1) is the clip-space point; un-project and divide.
    float2 ndc = uv * 2.0 - 1.0;
    float4 worldH = mul(g_InvViewProj, float4(ndc, depth, 1.0));
    return worldH.xyz / worldH.w;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    int3 px = int3(int2(input.position.xy), 0);

    float4 rawNormal = g_Normal.Load(px);

    float3 albedo = g_Albedo.Load(px).rgb;
    float3 orm    = g_ORM.Load(px).rgb;
    float  depth  = g_Depth.Load(px).r;

    float3 worldPos = ReconstructWorldPos(input.uv, depth);

    // orm.g is glTF perceptual roughness; clamp the low end so the specular V term
    // (0.5 / (GGXV + GGXL)) can't divide by zero on smooth surfaces at grazing angles.
    // IBL keeps the raw value: it never evaluates that term, and the floor would drag a
    // mirror 0.045*(mipCount-1) off mip 0 — visible extra blur for no reason.
    float  perceptualRoughness = max(orm.g, 0.045);
    float  iblRoughness        = saturate(orm.g);
    float  metallic  = orm.b;
    float  ao        = orm.r;

    float3 N = normalize(rawNormal.xyz);
    float3 eye = mul(g_InvView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float3 V = normalize(eye - worldPos);

    // Dielectrics get a fixed 4% F0 (reflectance 0.5); metals tint F0 with base color and
    // have no diffuse. Material-driven reflectance (orm.a) lands in a later step.
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 diffuseColor = (1.0 - metallic) * albedo;

    // Accumulate every scene light. EvaluateLight resolves L + incident radiance per
    // light type (Lib/Lights.hlsli); this pass only glues that onto the BRDF.
    float3 color = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < g_LightCount; ++i)
    {
        LightData light = GetLight(i);
        float3 L;
        float3 radiance = EvaluateLight(light, worldPos, L);
        color += EvaluateBRDF(N, V, L, diffuseColor, F0, perceptualRoughness) * radiance;
    }

    // g_EnvIntensity also scales the visible sky (Skybox.hlsl), so the two stay in step.
    if (HasEnvironmentIBL())
    {
        color += EvaluateIBL(N, V, diffuseColor, F0, iblRoughness, ao) * g_EnvIntensity;
    }
    else
    {
        color += g_Ambient * albedo * ao;
    }

    // Emissive is view-independent radiance the surface adds on its own — not lit, just
    // added on top (so it glows even in shadow / with no lights).
    color += g_Emissive.Load(px).rgb;

    return float4(color, 1.0);
}
