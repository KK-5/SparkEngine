// The material every surface uses today: glTF metallic-roughness driven by g_Materials. A
// generated material replaces the bodies of CalcPixelMaterialInputs and
// GetMaterialWorldPositionOffset; the signatures stay.
//
// Nothing here is bound: the sampler arrives as an argument, so the pass owns that register.
#ifndef SPARK_MATERIAL_TEMPLATE_HLSLI
#define SPARK_MATERIAL_TEMPLATE_HLSLI

#include <Shaders/MaterialBindings.hlsli>
#include <Shaders/Material/MaterialParameters.hlsli>

//! UE's default; pixels whose mask falls below it are clipped.
static const float kOpacityMaskClipValue = 1.0 / 3.0;

float3 GetMaterialWorldPositionOffset(MaterialVertexParameters parameters)
{
    return float3(0.0, 0.0, 0.0);
}

// Sampling is separate from material math: raster uses derivatives, ray hits supply an
// explicit mip level. Both paths evaluate the same factors, texture channels and normal map.
struct MaterialTextureSamples
{
    float4 BaseColor;
    float4 MetallicRoughness;
    float4 Normal;
    float4 Occlusion;
    float4 Emissive;
};

float4 SampleMaterialTexture(uint index, float2 uv, SamplerState materialSampler)
{
    if (index == SPARK_INVALID_TEXTURE_INDEX)
    {
        return 1.0;
    }
    Texture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(index)];
    return texture.Sample(materialSampler, uv);
}

float4 SampleMaterialTextureLevel(uint index, float2 uv, SamplerState materialSampler, float mipLevel)
{
    if (index == SPARK_INVALID_TEXTURE_INDEX)
    {
        return 1.0;
    }
    Texture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(index)];
    return texture.SampleLevel(materialSampler, uv, mipLevel);
}

float3 EvaluateMaterialEmission(MaterialData mat, float3 emissiveTexel)
{
    return mat.Emissive.rgb * mat.Emissive.a * emissiveTexel;
}

// Light sampling only needs emission, so avoid fetching the other material maps.
float3 EvaluateMaterialEmissionLevel(
    MaterialPixelParameters parameters, SamplerState materialSampler, float mipLevel)
{
    const MaterialData mat = GetMaterialData(parameters.MaterialIndex);
    const float3 emissiveTexel = SampleMaterialTextureLevel(
        mat.TexIndices[SPARK_TEX_SLOT_EMISSIVE], parameters.TexCoord0, materialSampler, mipLevel).rgb;
    return EvaluateMaterialEmission(mat, emissiveTexel);
}

PixelMaterialInputs EvaluateMaterialInputs(
    MaterialPixelParameters parameters, MaterialData mat, MaterialTextureSamples samples)
{
    PixelMaterialInputs inputs;
    inputs.Specular    = mat.Specular;
    inputs.OpacityMask = 1.0;

    // glTF: the base colour's alpha is the surface's opacity.
    float4 baseColor = mat.BaseColor * samples.BaseColor;
    inputs.BaseColor = baseColor.rgb;
    inputs.Opacity   = baseColor.a;

    // glTF metallic-roughness: G = roughness, B = metallic.
    inputs.Roughness = mat.Roughness * samples.MetallicRoughness.g;
    inputs.Metallic  = mat.Metallic * samples.MetallicRoughness.b;
    inputs.AmbientOcclusion = samples.Occlusion.r;

    inputs.Normal               = parameters.WorldVertexNormal;
    inputs.NormalIsTangentSpace = false;
    uint normalTexIndex = mat.TexIndices[SPARK_TEX_SLOT_NORMAL];
    if (normalTexIndex != SPARK_INVALID_TEXTURE_INDEX)
    {
        float3 normalTS = samples.Normal.xyz * 2.0 - 1.0;
        // glTF normalScale scales the tangent XY; CalcMaterialParameters renormalizes.
        normalTS.xy *= mat.NormalScale;
        inputs.Normal               = normalTS;
        inputs.NormalIsTangentSpace = true;
    }

    inputs.EmissiveColor = EvaluateMaterialEmission(mat, samples.Emissive.rgb);

    return inputs;
}

PixelMaterialInputs CalcPixelMaterialInputs(MaterialPixelParameters parameters, SamplerState materialSampler)
{
    MaterialData mat = GetMaterialData(parameters.MaterialIndex);
    float2 uv = parameters.TexCoord0;
    MaterialTextureSamples samples;
    samples.BaseColor = SampleMaterialTexture(mat.TexIndices[SPARK_TEX_SLOT_BASE_COLOR], uv, materialSampler);
    samples.MetallicRoughness = SampleMaterialTexture(mat.TexIndices[SPARK_TEX_SLOT_METALLIC_ROUGH], uv, materialSampler);
    samples.Normal = SampleMaterialTexture(mat.TexIndices[SPARK_TEX_SLOT_NORMAL], uv, materialSampler);
    samples.Occlusion = SampleMaterialTexture(mat.TexIndices[SPARK_TEX_SLOT_OCCLUSION], uv, materialSampler);
    samples.Emissive = SampleMaterialTexture(mat.TexIndices[SPARK_TEX_SLOT_EMISSIVE], uv, materialSampler);
    return EvaluateMaterialInputs(parameters, mat, samples);
}

//! Explicit LOD for compute/ray-hit shading; no screen derivatives or implicit footprint.
PixelMaterialInputs CalcPixelMaterialInputsLevel(
    MaterialPixelParameters parameters, SamplerState materialSampler, float mipLevel)
{
    MaterialData mat = GetMaterialData(parameters.MaterialIndex);
    float2 uv = parameters.TexCoord0;
    MaterialTextureSamples samples;
    samples.BaseColor = SampleMaterialTextureLevel(mat.TexIndices[SPARK_TEX_SLOT_BASE_COLOR], uv, materialSampler, mipLevel);
    samples.MetallicRoughness = SampleMaterialTextureLevel(mat.TexIndices[SPARK_TEX_SLOT_METALLIC_ROUGH], uv, materialSampler, mipLevel);
    samples.Normal = SampleMaterialTextureLevel(mat.TexIndices[SPARK_TEX_SLOT_NORMAL], uv, materialSampler, mipLevel);
    samples.Occlusion = SampleMaterialTextureLevel(mat.TexIndices[SPARK_TEX_SLOT_OCCLUSION], uv, materialSampler, mipLevel);
    samples.Emissive = SampleMaterialTextureLevel(mat.TexIndices[SPARK_TEX_SLOT_EMISSIVE], uv, materialSampler, mipLevel);
    return EvaluateMaterialInputs(parameters, mat, samples);
}

void ResolveMaterialNormal(inout MaterialPixelParameters parameters, PixelMaterialInputs inputs)
{
    if (inputs.NormalIsTangentSpace)
    {
        float3 N = parameters.WorldVertexNormal;
        float3 T = normalize(parameters.WorldTangent.xyz - dot(parameters.WorldTangent.xyz, N) * N);
        float3 B = cross(N, T) * parameters.WorldTangent.w;
        parameters.WorldNormal = normalize(mul(inputs.Normal, float3x3(T, B, N)));
    }
    else
    {
        parameters.WorldNormal = inputs.Normal;
    }

    // The back of a double-sided surface faces the other way, bumps and all.
    parameters.WorldNormal *= parameters.TwoSidedSign;
}

//! Evaluates the material and resolves what depends on it, the world normal today.
void CalcMaterialParameters(
    inout MaterialPixelParameters parameters, out PixelMaterialInputs inputs, SamplerState materialSampler)
{
    inputs = CalcPixelMaterialInputs(parameters, materialSampler);
    ResolveMaterialNormal(parameters, inputs);
}

void CalcMaterialParametersLevel(
    inout MaterialPixelParameters parameters, out PixelMaterialInputs inputs,
    SamplerState materialSampler, float mipLevel)
{
    inputs = CalcPixelMaterialInputsLevel(parameters, materialSampler, mipLevel);
    ResolveMaterialNormal(parameters, inputs);
}

//! Feed to clip(): negative discards.
float GetMaterialMask(PixelMaterialInputs inputs)
{
    return inputs.OpacityMask - kOpacityMaskClipValue;
}

#endif // SPARK_MATERIAL_TEMPLATE_HLSLI
