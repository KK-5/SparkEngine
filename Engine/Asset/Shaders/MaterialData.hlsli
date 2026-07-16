// Single definition source for the per-material GPU record. Any shader needing
// per-material data includes this (directly or via MaterialBindings.hlsl).
// C++ mirror lives in Render/MaterialBind/MaterialData.h — keep the two identical.
#ifndef SPARK_MATERIAL_DATA_HLSLI
#define SPARK_MATERIAL_DATA_HLSLI

struct MaterialData
{
    float4 BaseColor;   // rgb (+a reserved)
    float  Metallic;
    float  Roughness;
    float  Specular;
    float  _Pad;
};

#endif // SPARK_MATERIAL_DATA_HLSLI
