#include <Shaders/ViewBindings.hlsli>
#include <Shaders/SceneBindings.hlsli>
#include <Shaders/Material/MaterialTemplate.hlsli>
#include <Shaders/VertexFactory/LocalVertexFactory.hlsli>

struct ScopeParameters
{
    uint viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

// DepthOnly.hlsl for a shadow view: the same vertex, placed in the shadow map the view renders
// into, which is a layer of the shadow map array every view of ShadowPass shares.

struct VSOutput
{
    precise float4 Position : SV_Position;
    uint           Layer    : SV_RenderTargetArrayIndex;
};

VSOutput VSMain(PositionOnlyVertexFactoryInput input)
{
    precise float4 worldPosition = VertexFactoryGetWorldPosition(input);
    MaterialVertexParameters vertexParameters = GetMaterialVertexParameters(input, worldPosition.xyz);
    worldPosition.xyz += GetMaterialWorldPositionOffset(vertexParameters);

    VSOutput output;
    output.Position = mul(GetView(g_Scope.viewIndex).viewProjection, worldPosition);
    output.Layer    = g_ViewShadowMaps[g_Scope.viewIndex];
    return output;
}

// No render target: the pixel shader writes nothing (depth comes from the rasterizer).
void PSMain(VSOutput input)
{
}
