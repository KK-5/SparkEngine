#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Lib/DeferredShadingCommon.hlsli>
#include <Shaders/Material/MaterialTemplate.hlsli>
#include <Shaders/VertexFactory/LocalVertexFactory.hlsli>
#include <Shaders/Lib/Velocity.hlsli>
#include <Shaders/Lib/SpecularAA.hlsli>

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
    float4 normal    : SV_Target0;  // GBufferNormal,    see Lib/DeferredShadingCommon.hlsli
    float4 surface   : SV_Target1;  // GBufferSurface
    float4 baseColor : SV_Target2;  // GBufferBaseColor
    float4 emissive  : SV_Target3;  // rgb HDR emissive, added directly in the lighting pass
    float4 velocity  : SV_Target4;  // rg NDC motion, see Lib/Velocity.hlsli
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
    // normal.a is PerObjectGBufferData in UE; nothing produces it here yet.
    output.normal    = float4(EncodeNormal(parameters.WorldNormal), 0.0);
    output.surface   = float4(inputs.Metallic, inputs.Specular, inputs.Roughness,
                              EncodeShadingModel(SHADINGMODELID_DEFAULT_LIT, 0));
    output.baseColor = float4(inputs.BaseColor, inputs.AmbientOcclusion);
    output.emissive  = float4(inputs.EmissiveColor, 1.0);
    return output;
}

PSOutput PSMain(VSOutput input)
{
    MaterialPixelParameters parameters = GetMaterialPixelParameters(input.Interpolants, input.Position);

    PixelMaterialInputs inputs;
    CalcMaterialParameters(parameters, inputs, g_MatSampler);

    // Here, not in the lighting pass: the derivatives must span one surface, and neighbouring
    // GBuffer texels may belong to different objects. The filter works on alpha^2; the GBuffer
    // stores perceptual roughness, whose square is alpha.
    float roughnessA2 = inputs.Roughness * inputs.Roughness;
    roughnessA2 *= roughnessA2;
    inputs.Roughness = sqrt(sqrt(CalculateSpecularAA(parameters.WorldNormal, roughnessA2)));

    PSOutput output = EncodeGBuffer(parameters, inputs);
    output.velocity = float4(CalcVelocity(input.ClipPosition, input.PrevClipPosition), 0.0, 0.0);
    return output;
}
