#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Material/MaterialTemplate.hlsli>
#include <Shaders/VertexFactory/LocalVertexFactory.hlsli>

struct ScopeParameters
{
    uint viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

// Depth only, no color target. Shared by DepthPrePass and ShadowPass: the view is whichever
// one g_Scope.viewIndex names, a camera's or a light's. GBuffer.hlsl depth-tests Equal against this, so both
// place the vertex through the same vertex factory and material calls.

struct VSOutput
{
    precise float4 Position : SV_Position;
};

VSOutput VSMain(PositionOnlyVertexFactoryInput input)
{
    precise float4 worldPosition = VertexFactoryGetWorldPosition(input);
    MaterialVertexParameters vertexParameters = GetMaterialVertexParameters(input, worldPosition.xyz);
    worldPosition.xyz += GetMaterialWorldPositionOffset(vertexParameters);

    VSOutput output;
    output.Position = mul(GetView(g_Scope.viewIndex).viewProjection, worldPosition);
    return output;
}

// No render target: the pixel shader writes nothing (depth comes from the rasterizer).
void PSMain(VSOutput input)
{
}
