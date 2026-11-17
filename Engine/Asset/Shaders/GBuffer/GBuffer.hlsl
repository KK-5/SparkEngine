#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Lib/DeferredShadingCommon.hlsli>
#include <Shaders/Material/MaterialTemplate.hlsli>
#include <Shaders/VertexFactory/LocalVertexFactory.hlsli>
#include <Shaders/Lib/Velocity.hlsli>
#include <Shaders/Lib/SpecularAA.hlsli>
#include <Shaders/Lib/Geometry/GeometryShading.hlsli>
#include <Shaders/Lib/Geometry/MeshGeometry.hlsli>

struct ScopeParameters
{
    uint viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

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
    float3 WorldPosition    : WORLD_POSITION;
    nointerpolation uint InstanceIndex : INSTANCE_INDEX;
};

struct PSOutput
{
    float4 sceneColor : SV_Target0;  // emissive; the lighting passes blend onto it
    float4 normal     : SV_Target1;  // GBufferNormal, see Lib/DeferredShadingCommon.hlsli
    float4 surface    : SV_Target2;  // GBufferSurface
    float4 baseColor  : SV_Target3;  // GBufferBaseColor
    float4 velocity   : SV_Target4;  // rg NDC motion, see Lib/Velocity.hlsli
    float4 geometricNormal : SV_Target5; // geometric, world-space, facing the camera
    float4 shadowOriginOffset : SV_Target6; // signed world-space displacement, alpha unused
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

    const ViewData view = GetView(g_Scope.viewIndex);

    VSOutput output;
    output.Position         = mul(view.viewProjection, worldPosition);
    output.Interpolants     = VertexFactoryGetInterpolantsVSToPS(input, intermediates);
    output.ClipPosition     = mul(view.viewProjectionNoAA, worldPosition);
    output.PrevClipPosition = mul(view.prevViewProjection, prevWorldPosition);
    output.WorldPosition = worldPosition.xyz;
    output.InstanceIndex = input.InstanceIndex;
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
    output.sceneColor = float4(inputs.EmissiveColor * GetView(g_Scope.viewIndex).preExposure, 1.0);
    return output;
}

// Material-dependent triangle loading stays at the raster entry point. The
// geometry helper only needs positions and normals, regardless of their source.
float3 GetShadowOriginOffset(VSOutput input, uint primitiveIndex, float3 geometricNormal,
                             float3 worldVertexNormal)
{
    const InstanceData instance = GetInstanceData(input.InstanceIndex);
    const GeometryData geometry = g_Geometries[instance.GeometryIndex];
    const MeshTriangle meshTriangle = LoadMeshTriangle(geometry, primitiveIndex);
    const MeshVertex vertices[3] = { meshTriangle.Vertex0, meshTriangle.Vertex1, meshTriangle.Vertex2 };
    float3 positions[3];
    float3 normals[3];
    // Orient all vertex normals together, including mirrored and double-sided meshes.
    const float normalSign = dot(worldVertexNormal, geometricNormal) < 0.0 ? -1.0 : 1.0;
    [unroll]
    for (uint i = 0u; i < 3u; ++i)
    {
        positions[i] = TransformLocalToWorld(instance, vertices[i].Position).xyz;
        MaterialVertexParameters vertexParameters;
        vertexParameters.WorldPosition = positions[i];
        vertexParameters.MaterialIndex = instance.MaterialIndex;
        positions[i] += GetMaterialWorldPositionOffset(vertexParameters);

        const float3 normal = mul((float3x3)instance.NormalMatrix, vertices[i].Normal);
        const float lengthSquared = dot(normal, normal);
        normals[i] = lengthSquared > 0.0 ? normal * (normalSign * rsqrt(lengthSquared)) : geometricNormal;
    }
    return ComputeShadowOriginOffset(input.WorldPosition, positions, normals);
}

PSOutput PSMain(VSOutput input, bool isFrontFace : SV_IsFrontFace, uint primitiveIndex : SV_PrimitiveID)
{
    // A single-sided surface's back is culled, so only a double-sided one reaches here back-facing.
    MaterialPixelParameters parameters = GetMaterialPixelParameters(input.Interpolants, input.Position, isFrontFace);

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
    const float3 eye = mul(GetView(g_Scope.viewIndex).invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    const float3 Ng = ComputeGeometricNormal(ddx(input.WorldPosition), ddy(input.WorldPosition),
        parameters.WorldVertexNormal, eye - input.WorldPosition);
    output.geometricNormal = float4(EncodeNormal(Ng), 0.0);
    output.shadowOriginOffset = float4(GetShadowOriginOffset(input, primitiveIndex, Ng,
        parameters.WorldVertexNormal), 0.0);
    return output;
}
