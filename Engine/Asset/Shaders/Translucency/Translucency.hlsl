#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Material/MaterialTemplate.hlsli>
#include <Shaders/VertexFactory/LocalVertexFactory.hlsli>

struct ScopeParameters
{
    uint viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

// Translucent surfaces, forward, blended over SceneColor by the pass's state. Unlit for now:
// the material's base colour at its opacity. No velocity: what is behind keeps its own.

SamplerState g_MatSampler : register(s0, space2);

struct VSOutput
{
    float4 Position : SV_Position;
    VertexFactoryInterpolantsVSToPS Interpolants;
};

VSOutput VSMain(VertexFactoryInput input)
{
    VertexFactoryIntermediates intermediates = GetVertexFactoryIntermediates(input);

    float4 worldPosition = VertexFactoryGetWorldPosition(input, intermediates);
    MaterialVertexParameters vertexParameters = GetMaterialVertexParameters(input, intermediates, worldPosition.xyz);
    worldPosition.xyz += GetMaterialWorldPositionOffset(vertexParameters);

    VSOutput output;
    output.Position     = mul(GetView(g_Scope.viewIndex).viewProjection, worldPosition);
    output.Interpolants = VertexFactoryGetInterpolantsVSToPS(input, intermediates);
    return output;
}

float4 PSMain(VSOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target0
{
    MaterialPixelParameters parameters = GetMaterialPixelParameters(input.Interpolants, input.Position, isFrontFace);

    PixelMaterialInputs inputs;
    CalcMaterialParameters(parameters, inputs, g_MatSampler);

    return float4(inputs.BaseColor * GetView(g_Scope.viewIndex).preExposure, inputs.Opacity);
}
