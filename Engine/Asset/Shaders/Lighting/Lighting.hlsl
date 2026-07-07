// Lighting.hlsl — deferred lighting pass. Full-screen triangle that reads the
// GBuffer (Albedo / Normal / ORM / Position) per pixel and shades a single
// hardcoded directional light with a Cook-Torrance BRDF, writing SceneColor.
//
// GBuffer is sampled with Load (integer pixel fetch, point sampling) — no sampler,
// no UV, no flip — the natural fit for a 1:1 full-res deferred read. Position is
// read straight from the GBuffer for now (TEMPORARY scaffold); once depth-as-SRV
// sampling lands this pass reconstructs world position from SceneDepth instead and
// the GBuffer Position target goes away.
//
// Sky / uncovered pixels: the Normal target is cleared to 0, so a ~zero-length
// normal means "no geometry here" — discard, leaving SceneColor at its clear value
// for the skybox pass to fill afterwards.

#include <Shaders/ViewBindings.hlsl>   // space0: g_InvView (camera world pos)

Texture2D g_Albedo   : register(t0, space1);
Texture2D g_Normal   : register(t1, space1);
Texture2D g_ORM      : register(t2, space1);
Texture2D g_Position : register(t3, space1);

static const float PI = 3.14159265359;

// Hardcoded directional light until the light system lands. Direction points FROM
// the light TO the scene; L below negates it to point toward the light.
static const float3 g_LightDir     = normalize(float3(-0.5, -1.0, -0.3));
static const float3 g_LightColor   = float3(1.0, 0.98, 0.95);
static const float  g_LightIntensity = 3.0;
static const float3 g_Ambient      = float3(0.03, 0.03, 0.03);

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    // Full-screen triangle from SV_VertexID: NDC (-1,-1),(3,-1),(-1,3).
    float2 uv  = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom  = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 Tonemap(float3 hdr)
{
    float3 mapped = hdr / (hdr + 1.0);  // Reinhard
    return pow(mapped, 1.0 / 2.2);      // linear -> sRGB-ish
}

float4 PSMain(VSOutput input) : SV_Target0
{
    int3 px = int3(int2(input.position.xy), 0);

    float4 rawNormal = g_Normal.Load(px);
    // No geometry wrote here (Normal cleared to 0) — leave it for the skybox.
    if (dot(rawNormal.xyz, rawNormal.xyz) < 1e-4)
    {
        discard;
    }

    float3 albedo   = g_Albedo.Load(px).rgb;
    float3 orm      = g_ORM.Load(px).rgb;
    float3 worldPos = g_Position.Load(px).xyz;

    float  roughness = orm.g;
    float  metallic  = orm.b;
    float  ao        = orm.r;

    float3 N = normalize(rawNormal.xyz);
    float3 eye = mul(g_InvView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float3 V = normalize(eye - worldPos);
    float3 L = normalize(-g_LightDir);
    float3 H = normalize(V + L);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float  D = DistributionGGX(N, H, roughness);
    float  G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float  NdotL = max(dot(N, L), 0.0);
    float  NdotV = max(dot(N, V), 0.0);
    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);

    float3 kd = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kd * albedo / PI;

    float3 radiance = g_LightColor * g_LightIntensity;
    float3 color = (diffuse + specular) * radiance * NdotL;
    color += g_Ambient * albedo * ao;

    return float4(Tonemap(color), 1.0);
}
