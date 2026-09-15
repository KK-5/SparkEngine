// Static mesh vertex factory: per-vertex attributes from the mesh buffers, transform from
// g_Instances. Every function a pass template calls on geometry lives here.
#ifndef SPARK_LOCAL_VERTEX_FACTORY_HLSLI
#define SPARK_LOCAL_VERTEX_FACTORY_HLSLI

#include <Shaders/InstanceBindings.hlsli>
#include <Shaders/Material/MaterialParameters.hlsli>

struct VertexFactoryInput
{
    float3 Position      : POSITION;        // slot 0, per-vertex
    float3 Normal        : NORMAL;          // slot 0
    float4 Tangent       : TANGENT;         // slot 0, w = handedness (MikkTSpace)
    float2 TexCoord0     : TEXCOORD0;       // slot 0
    uint   InstanceIndex : INSTANCE_INDEX;  // slot 1, per-instance
};

//! For passes that bind the position stream only.
struct PositionOnlyVertexFactoryInput
{
    float3 Position      : POSITION;
    uint   InstanceIndex : INSTANCE_INDEX;
};

struct VertexFactoryIntermediates
{
    InstanceData Instance;
};

struct VertexFactoryInterpolantsVSToPS
{
    float3 WorldNormal  : NORMAL;
    float4 WorldTangent : TANGENT;
    float2 TexCoord0    : TEXCOORD0;
    nointerpolation uint MaterialIndex : MATERIAL_INDEX;
};

//! The one place a position is transformed, so every input form yields bit-identical depth
//! (GBufferPass depth-tests Equal against DepthPrePass).
float4 TransformLocalToWorld(InstanceData instance, float3 position)
{
    return mul(instance.Model, float4(position, 1.0));
}

VertexFactoryIntermediates GetVertexFactoryIntermediates(VertexFactoryInput input)
{
    VertexFactoryIntermediates intermediates;
    intermediates.Instance = GetInstanceData(input.InstanceIndex);
    return intermediates;
}

float4 VertexFactoryGetWorldPosition(VertexFactoryInput input, VertexFactoryIntermediates intermediates)
{
    return TransformLocalToWorld(intermediates.Instance, input.Position);
}

float4 VertexFactoryGetWorldPosition(PositionOnlyVertexFactoryInput input)
{
    return TransformLocalToWorld(GetInstanceData(input.InstanceIndex), input.Position);
}

//! Current position until InstanceData carries the previous transform.
float4 VertexFactoryGetPreviousWorldPosition(VertexFactoryInput input, VertexFactoryIntermediates intermediates)
{
    return VertexFactoryGetWorldPosition(input, intermediates);
}

MaterialVertexParameters GetMaterialVertexParameters(
    VertexFactoryInput input, VertexFactoryIntermediates intermediates, float3 worldPosition)
{
    MaterialVertexParameters parameters;
    parameters.WorldPosition = worldPosition;
    parameters.MaterialIndex = intermediates.Instance.MaterialIndex;
    return parameters;
}

MaterialVertexParameters GetMaterialVertexParameters(PositionOnlyVertexFactoryInput input, float3 worldPosition)
{
    MaterialVertexParameters parameters;
    parameters.WorldPosition = worldPosition;
    parameters.MaterialIndex = GetInstanceData(input.InstanceIndex).MaterialIndex;
    return parameters;
}

VertexFactoryInterpolantsVSToPS VertexFactoryGetInterpolantsVSToPS(
    VertexFactoryInput input, VertexFactoryIntermediates intermediates)
{
    VertexFactoryInterpolantsVSToPS interpolants;
    // Normals take the inverse-transpose; tangents lie in the surface and take Model.
    interpolants.WorldNormal   = mul((float3x3)intermediates.Instance.NormalMatrix, input.Normal);
    interpolants.WorldTangent  = float4(mul((float3x3)intermediates.Instance.Model, input.Tangent.xyz), input.Tangent.w);
    interpolants.TexCoord0     = input.TexCoord0;
    interpolants.MaterialIndex = intermediates.Instance.MaterialIndex;
    return interpolants;
}

MaterialPixelParameters GetMaterialPixelParameters(VertexFactoryInterpolantsVSToPS interpolants, float4 svPosition)
{
    MaterialPixelParameters parameters;
    parameters.SvPosition        = svPosition;
    parameters.WorldVertexNormal = normalize(interpolants.WorldNormal);
    parameters.WorldTangent      = interpolants.WorldTangent;
    parameters.TexCoord0         = interpolants.TexCoord0;
    parameters.MaterialIndex     = interpolants.MaterialIndex;
    parameters.WorldNormal       = parameters.WorldVertexNormal;
    return parameters;
}

#endif // SPARK_LOCAL_VERTEX_FACTORY_HLSLI
