// Indirect specular. Full-screen triangle that adds the radiance arriving along the reflection
// to SceneColor, through the split-sum approximation: the prefiltered environment, and over it
// the screen-space reflections as far as their alpha trusts them. Ray-traced reflections later
// take the pixels that alpha leaves, before the cube.
//
// Sky pixels are culled by the rasterizer (far-plane triangle, depth-test Less against
// SceneDepth).

#include <Shaders/ViewBindings.hlsli>
#include <Shaders/SceneBindings.hlsli>
#include <Shaders/Lib/DeferredShadingCommon.hlsli>
#include <Shaders/Lib/AmbientOcclusion.hlsli>
#include <Shaders/Lib/BRDF/EnvBRDF.hlsli>

struct ScopeParameters
{
    uint viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

Texture2D        g_GBufferNormal    : register(t0, space2);
Texture2D        g_GBufferSurface   : register(t1, space2);
Texture2D        g_GBufferBaseColor : register(t2, space2);
Texture2D        g_Depth            : register(t3, space2);   // SceneDepth, viewed as R32_FLOAT
Texture2D<float> g_AmbientOcclusion : register(t4, space2);   // bound only while the frame has it
Texture2D<float4> g_ScreenSpaceReflections : register(t5, space2);   // likewise

cbuffer ReflectionsParams : register(b0, space2)
{
    uint g_AmbientOcclusionEnabled;         // 0: g_AmbientOcclusion is unbound and must not be read
    uint g_ScreenSpaceReflectionsEnabled;   // 0: g_ScreenSpaceReflections likewise
};

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

    GBufferData gbuffer = GetGBufferData(
        g_GBufferNormal, g_GBufferSurface, g_GBufferBaseColor, g_Depth,
        int2(input.position.xy));

    float3 color = float3(0.0, 0.0, 0.0);
    if (HasEnvironmentIBL())
    {
        float3 worldPos = ReconstructWorldPos(view, input.uv, gbuffer.Depth);
        float3 N = normalize(gbuffer.WorldNormal);
        float3 eye = mul(view.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
        float3 V = normalize(eye - worldPos);
        float NoV = max(abs(dot(N, V)), 1e-4);

        // Unclamped, unlike the analytic BRDF's 0.045 floor: that floor would drag a mirror
        // 0.045*(mipCount-1) off mip 0, and this path never evaluates the visibility term.
        float roughness = saturate(gbuffer.Roughness);

        // Explicit LOD: the mips are a roughness axis, not a detail chain.
        float3 R   = reflect(-V, N);
        float  lod = RoughnessToLod(roughness, g_IBLPrefilteredMipCount);
        float3 prefiltered = g_PrefilteredCube.SampleLevel(g_IBLSampler, R, lod).rgb;

        // The material's occlusion map times the screen-space signal, turned into the
        // occlusion of the reflection lobe. Uniform across the draw.
        float ao = gbuffer.GBufferAO;
        if (g_AmbientOcclusionEnabled != 0)
        {
            ao *= g_AmbientOcclusion.Load(int3(input.position.xy, 0));
        }

        // The occlusion stands in for what the cube cannot know is in the way. A traced
        // reflection found what is in the way, so it takes none.
        float3 radiance = prefiltered * g_EnvIntensity * SpecularOcclusion(NoV, roughness, ao);
        if (g_ScreenSpaceReflectionsEnabled != 0)
        {
            float4 traced = g_ScreenSpaceReflections.Load(int3(input.position.xy, 0));
            radiance = lerp(radiance, traced.rgb, traced.a);
        }

        color = radiance * EnvBRDFLut(g_BRDFLut, g_IBLSampler, gbuffer.SpecularColor, roughness, NoV);
    }

    color += SpaceZeroKeepAlive();

    // Alpha is held by the blend state, so what is written here never lands.
    return float4(color * view.preExposure, 0.0);
}
