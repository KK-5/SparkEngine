// Single definition source for the per-material GPU record. Any shader needing
// per-material data includes this (directly or via MaterialBindings.hlsl).
// C++ mirror lives in Render/MaterialBind/MaterialData.h — keep the two identical.
#ifndef SPARK_MATERIAL_DATA_HLSLI
#define SPARK_MATERIAL_DATA_HLSLI

// "No texture" sentinel — mirrors Render::InvalidTextureIndex / RHI bindless -1.
#define SPARK_INVALID_TEXTURE_INDEX 0xffffffffu

struct MaterialData
{
    float4 BaseColor;         // rgb (+a reserved)
    float  Metallic;
    float  Roughness;
    float  Specular;
    uint   BaseColorTexIndex; // SM6.6 bindless heap index, or SPARK_INVALID_TEXTURE_INDEX
};

#endif // SPARK_MATERIAL_DATA_HLSLI
