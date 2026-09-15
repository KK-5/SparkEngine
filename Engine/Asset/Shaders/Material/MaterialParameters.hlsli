// The data a material reads and writes, independent of which material or vertex factory is
// compiled in. A vertex factory fills the parameters; a material template fills the inputs.
#ifndef SPARK_MATERIAL_PARAMETERS_HLSLI
#define SPARK_MATERIAL_PARAMETERS_HLSLI

struct MaterialVertexParameters
{
    float3 WorldPosition;
    uint   MaterialIndex;
};

struct MaterialPixelParameters
{
    float4 SvPosition;
    float3 WorldVertexNormal;   // interpolated, normalized
    float4 WorldTangent;        // xyz interpolated, w = handedness
    float2 TexCoord0;
    uint   MaterialIndex;

    float3 WorldNormal;         // resolved by CalcMaterialParameters
};

struct PixelMaterialInputs
{
    float3 BaseColor;
    float  Metallic;
    float  Specular;
    float  Roughness;
    float3 EmissiveColor;
    float  AmbientOcclusion;
    float  Opacity;
    float  OpacityMask;

    float3 Normal;
    bool   NormalIsTangentSpace;   // false: Normal is already world space
};

#endif // SPARK_MATERIAL_PARAMETERS_HLSLI
