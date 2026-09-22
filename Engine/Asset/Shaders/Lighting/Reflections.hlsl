// Indirect specular. Full-screen triangle that adds the prefiltered environment through the
// split-sum approximation to SceneColor. This is where SSR or ray-traced reflections later
// layer over the cube, falling back to it where the trace fails.
//
// Sky pixels are culled by the rasterizer (far-plane triangle, depth-test Less against
// SceneDepth).

#include <Shaders/ViewBindings.hlsli>
#include <Shaders/SceneBindings.hlsli>
#include <Shaders/Lib/DeferredShadingCommon.hlsli>
#include <Shaders/Lib/BRDF/EnvBRDF.hlsli>

Texture2D g_GBufferNormal    : register(t0, space2);
Texture2D g_GBufferSurface   : register(t1, space2);
Texture2D g_GBufferBaseColor : register(t2, space2);
Texture2D g_Depth            : register(t3, space2);   // SceneDepth, viewed as R32_FLOAT

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

    float3 color = float3(0.0, 0.0, 0.0);
    if (HasEnvironmentIBL())
    {
        float3 worldPos = ReconstructWorldPos(input.uv, gbuffer.Depth);
        float3 N = normalize(gbuffer.WorldNormal);
        float3 eye = mul(g_InvView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
        float3 V = normalize(eye - worldPos);
        float NoV = max(abs(dot(N, V)), 1e-4);

        // Unclamped, unlike the analytic BRDF's 0.045 floor: that floor would drag a mirror
        // 0.045*(mipCount-1) off mip 0, and this path never evaluates the visibility term.
        float roughness = saturate(gbuffer.Roughness);

        // Explicit LOD: the mips are a roughness axis, not a detail chain.
        float3 R   = reflect(-V, N);
        float  lod = RoughnessToLod(roughness, g_IBLPrefilteredMipCount);
        float3 prefiltered = g_PrefilteredCube.SampleLevel(g_IBLSampler, R, lod).rgb;

        // AO on specular too: strictly that wants a specular-occlusion term, but leaving it
        // unoccluded makes AO vanish entirely on metals.
        color = prefiltered
              * EnvBRDFLut(g_BRDFLut, g_IBLSampler, gbuffer.SpecularColor, roughness, NoV)
              * g_EnvIntensity * gbuffer.GBufferAO;
    }

    color += SpaceZeroKeepAlive();

    // Alpha is held by the blend state, so what is written here never lands.
    return float4(color * g_PreExposure, 0.0);
}
