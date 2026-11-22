#include "DirectLightingComposite.hlsli"

struct ScopeParameters
{
    uint viewIndex;
};
#include <Shaders/ScopeBindings.hlsli>

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position
{
    return DirectLightingFullscreenPosition(vertexId);
}

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    return CompositeRawDirectLighting(int2(position.xy), g_Scope.viewIndex);
}
