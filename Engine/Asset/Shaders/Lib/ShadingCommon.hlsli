// What the default-lit shading model derives from a surface, wherever the surface comes from:
// the GBuffer or a material evaluated in place. Corresponds to UE's ShadingCommon.ush.
#ifndef SPARK_LIB_SHADING_COMMON_HLSLI
#define SPARK_LIB_SHADING_COMMON_HLSLI

//! Metals have no diffuse.
float3 ComputeDiffuseColor(float3 baseColor, float metallic)
{
    return baseColor * (1.0 - metallic);
}

//! Metals tint F0 with base color; dielectrics keep a flat F0, 0.5 specular being the usual 0.04.
float3 ComputeF0(float specular, float3 baseColor, float metallic)
{
    return lerp(0.08 * specular, baseColor, metallic);
}

#endif // SPARK_LIB_SHADING_COMMON_HLSLI
