#include <NRD.hlsli>
#include "DirectLightingSignal.hlsli"

struct ScopeParameters
{
    uint2 inputSize;
};

#include <Shaders/ScopeBindings.hlsli>

Texture2D<float4> g_RawDiffuse : register(t0, space2);
Texture2D<float4> g_RawSpecular : register(t1, space2);
Texture2D<float4> g_RawNormalRoughness : register(t2, space2);
Texture2D<float2> g_RawMotion : register(t3, space2);

RWTexture2D<float4> g_Diffuse : register(u0, space2);
RWTexture2D<float4> g_Specular : register(u1, space2);
RWTexture2D<float4> g_NormalRoughness : register(u2, space2);
RWTexture2D<float4> g_Motion : register(u3, space2);
RWTexture2D<float> g_HistoryConfidence : register(u4, space2);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThread : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThread.xy;
    if (any(pixel >= g_Scope.inputSize))
    {
        return;
    }

    // Both inputs use neutral confidence until lighting gradients are available.
    if (all(pixel == 0))
    {
        g_HistoryConfidence[pixel] = 1.0;
    }

    const float4 diffuse = g_RawDiffuse[pixel];
    const float4 specular = g_RawSpecular[pixel];
    const float4 normalRoughness = g_RawNormalRoughness[pixel];
    const float perceptualRoughness = normalRoughness.w;

    // RELAX expects hit distance in scene units.
    g_Diffuse[pixel] = RELAX_FrontEnd_PackRadianceAndHitDist(diffuse.rgb, diffuse.w, true);
    g_Specular[pixel] = RELAX_FrontEnd_PackRadianceAndHitDist(specular.rgb, specular.w, true);
    g_NormalRoughness[pixel] = NRD_FrontEnd_PackNormalAndRoughness(normalRoughness.xyz, perceptualRoughness, 0.0);

    float2 motion = g_RawMotion[pixel];
    if (!all(isfinite(motion)) || all(motion == kDirectLightingInvalidMotion))
    {
        motion = 0.0;
    }
    g_Motion[pixel] = float4(motion, 0.0, 0.0);
}
