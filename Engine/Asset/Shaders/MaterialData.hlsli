// Single definition source for the per-material GPU record. Any shader needing
// per-material data includes this (directly or via MaterialBindings.hlsl).
// C++ mirror lives in Render/Binding/Material/MaterialData.h — keep the two identical.
#ifndef SPARK_MATERIAL_DATA_HLSLI
#define SPARK_MATERIAL_DATA_HLSLI

// "No texture" sentinel — mirrors Render::InvalidTextureIndex / RHI bindless -1.
#define SPARK_INVALID_TEXTURE_INDEX 0xffffffffu

// Texture slots — mirror Material::MaterialTexSlot (order is the GPU contract).
#define SPARK_MATERIAL_TEX_SLOT_COUNT   5
#define SPARK_TEX_SLOT_BASE_COLOR       0
#define SPARK_TEX_SLOT_METALLIC_ROUGH   1
#define SPARK_TEX_SLOT_NORMAL           2
#define SPARK_TEX_SLOT_OCCLUSION        3
#define SPARK_TEX_SLOT_EMISSIVE         4

struct MaterialData
{
    float4 BaseColor;         // rgb (+a reserved)
    float  Metallic;
    float  Roughness;
    float  Specular;
    float  NormalScale;       // tangent XY scale for the normal map
    float4 Emissive;          // rgb = emissive factor, a = strength

    uint   TexIndices[SPARK_MATERIAL_TEX_SLOT_COUNT];
};

#endif // SPARK_MATERIAL_DATA_HLSLI
