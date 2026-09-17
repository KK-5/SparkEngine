#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Material/MaterialTemplate.hlsli>
#include <Shaders/VertexFactory/LocalVertexFactory.hlsli>
#include <Shaders/Lib/Velocity.hlsli>

// Deferred base pass. Geometry comes from the vertex factory and the surface from the material
// template; this file places the vertex and encodes the GBuffer. It depth-tests Equal against
// DepthPrePass's SceneDepth, so the position must be computed exactly as DepthOnly.hlsl does.

SamplerState g_MatSampler : register(s0, space2);

struct VSOutput
{
    precise float4 Position : SV_Position;
    VertexFactoryInterpolantsVSToPS Interpolants;
    float4 ClipPosition     : CLIP_POSITION;        // unjittered
    float4 PrevClipPosition : PREV_CLIP_POSITION;   // last frame, unjittered
};

struct PSOutput
{
    float4 albedo   : SV_Target0;  // rgb base color
    float4 normal   : SV_Target1;  // xyz world-space normal, stored raw in [-1, 1]
    float4 orm      : SV_Target2;  // r = occlusion, g = roughness, b = metallic
    float4 emissive : SV_Target3;  // rgb HDR emissive, added directly in the lighting pass
    float4 velocity : SV_Target4;  // rg NDC motion, see Lib/Velocity.hlsli
};

VSOutput VSMain(VertexFactoryInput input)
{
    VertexFactoryIntermediates intermediates = GetVertexFactoryIntermediates(input);

    precise float4 worldPosition = VertexFactoryGetWorldPosition(input, intermediates);
    MaterialVertexParameters vertexParameters = GetMaterialVertexParameters(input, intermediates, worldPosition.xyz);
    worldPosition.xyz += GetMaterialWorldPositionOffset(vertexParameters);

    // The offset takes no time yet, so evaluating it at last frame's position is the whole
    // of last frame's offset.
    float4 prevWorldPosition = VertexFactoryGetPreviousWorldPosition(input, intermediates);
    MaterialVertexParameters prevVertexParameters = GetMaterialVertexParameters(input, intermediates, prevWorldPosition.xyz);
    prevWorldPosition.xyz += GetMaterialWorldPositionOffset(prevVertexParameters);

    VSOutput output;
    output.Position         = mul(g_ViewProjection, worldPosition);
    output.Interpolants     = VertexFactoryGetInterpolantsVSToPS(input, intermediates);
    output.ClipPosition     = mul(g_ViewProjectionNoAA, worldPosition);
    output.PrevClipPosition = mul(g_PrevViewProjection, prevWorldPosition);
    return output;
}

PSOutput EncodeGBuffer(MaterialPixelParameters parameters, PixelMaterialInputs inputs)
{
    PSOutput output = (PSOutput)0;
    output.albedo   = float4(inputs.BaseColor, 1.0);
    output.normal   = float4(parameters.WorldNormal, 0.0);
    output.orm      = float4(inputs.AmbientOcclusion, inputs.Roughness, inputs.Metallic, 1.0);
    output.emissive = float4(inputs.EmissiveColor, 1.0);
    return output;
}

PSOutput PSMain(VSOutput input)
{
    MaterialPixelParameters parameters = GetMaterialPixelParameters(input.Interpolants, input.Position);

    PixelMaterialInputs inputs;
    CalcMaterialParameters(parameters, inputs, g_MatSampler);

    PSOutput output = EncodeGBuffer(parameters, inputs);
    output.velocity = float4(CalcVelocity(input.ClipPosition, input.PrevClipPosition), 0.0, 0.0);
    return output;
}
