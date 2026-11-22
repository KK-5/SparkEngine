#include <NRD.hlsli>
#include "DirectLightingComposite.hlsli"

struct ScopeParameters
{
    uint viewIndex;
    float nrdDenoisingRange;
    uint2 nrdRectSize;
};
#include <Shaders/ScopeBindings.hlsli>

Texture2D<float4> g_NRDDiffuse : register(t4, space2);
Texture2D<float4> g_NRDSpecular : register(t5, space2);
Texture2D<float> g_LightingViewZ : register(t6, space2);

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position
{
    return DirectLightingFullscreenPosition(vertexId);
}

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    const int2 pixel = int2(position.xy);
    const int3 location = int3(pixel, 0);

    // REBLUR can leave pixels outside the active rect or denoising range unwritten.
    [branch]
    if (all(uint2(pixel) < g_Scope.nrdRectSize)
        && g_LightingViewZ.Load(location) < g_Scope.nrdDenoisingRange)
    {
        const float3 diffuse = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(g_NRDDiffuse.Load(location)).rgb;
        const float3 specular = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(g_NRDSpecular.Load(location)).rgb;
        return CompositeDirectLighting(pixel, diffuse, specular, g_Scope.viewIndex);
    }

    return CompositeRawDirectLighting(pixel, g_Scope.viewIndex);
}
