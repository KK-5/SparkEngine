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

PixelMaterialInputs CalcPixelMaterialInputs(MaterialPixelParameters parameters, SamplerState materialSampler)
{
    MaterialData mat = GetMaterialData(parameters.MaterialIndex);
    float2 uv = parameters.TexCoord0;

    PixelMaterialInputs inputs;
    inputs.Specular    = mat.Specular;
    inputs.Opacity     = 1.0;
    inputs.OpacityMask = 1.0;

    inputs.BaseColor = mat.BaseColor.rgb;
    uint baseColorTexIndex = mat.TexIndices[SPARK_TEX_SLOT_BASE_COLOR];
    if (baseColorTexIndex != SPARK_INVALID_TEXTURE_INDEX)
    {
        Texture2D<float4> baseColorTex = ResourceDescriptorHeap[NonUniformResourceIndex(baseColorTexIndex)];
        inputs.BaseColor *= baseColorTex.Sample(materialSampler, uv).rgb;
    }

    inputs.Roughness = mat.Roughness;
    inputs.Metallic  = mat.Metallic;
    uint MRTexIndex = mat.TexIndices[SPARK_TEX_SLOT_METALLIC_ROUGH];
    if (MRTexIndex != SPARK_INVALID_TEXTURE_INDEX)
    {
        // glTF metallic-roughness: G = roughness, B = metallic.
        Texture2D<float4> MRTex = ResourceDescriptorHeap[NonUniformResourceIndex(MRTexIndex)];
        float4 mr = MRTex.Sample(materialSampler, uv);
        inputs.Roughness *= mr.g;
        inputs.Metallic  *= mr.b;
    }

    inputs.AmbientOcclusion = 1.0;
    uint occlusionTexIndex = mat.TexIndices[SPARK_TEX_SLOT_OCCLUSION];
    if (occlusionTexIndex != SPARK_INVALID_TEXTURE_INDEX)
    {
        Texture2D<float4> occlusionTex = ResourceDescriptorHeap[NonUniformResourceIndex(occlusionTexIndex)];
        inputs.AmbientOcclusion = occlusionTex.Sample(materialSampler, uv).r;
    }

    inputs.Normal               = parameters.WorldVertexNormal;
    inputs.NormalIsTangentSpace = false;
    uint normalTexIndex = mat.TexIndices[SPARK_TEX_SLOT_NORMAL];
    if (normalTexIndex != SPARK_INVALID_TEXTURE_INDEX)
    {
        Texture2D<float4> normalTex = ResourceDescriptorHeap[NonUniformResourceIndex(normalTexIndex)];
        float3 normalTS = normalTex.Sample(materialSampler, uv).xyz * 2.0 - 1.0;
        // glTF normalScale scales the tangent XY; CalcMaterialParameters renormalizes.
        normalTS.xy *= mat.NormalScale;
        inputs.Normal               = normalTS;
        inputs.NormalIsTangentSpace = true;
    }

    inputs.EmissiveColor = mat.Emissive.rgb * mat.Emissive.a;
    uint emissiveTexIndex = mat.TexIndices[SPARK_TEX_SLOT_EMISSIVE];
    if (emissiveTexIndex != SPARK_INVALID_TEXTURE_INDEX)
    {
        Texture2D<float4> emissiveTex = ResourceDescriptorHeap[NonUniformResourceIndex(emissiveTexIndex)];
        inputs.EmissiveColor *= emissiveTex.Sample(materialSampler, uv).rgb;
    }

    return inputs;
}

//! Evaluates the material and resolves what depends on it, the world normal today.
void CalcMaterialParameters(
    inout MaterialPixelParameters parameters, out PixelMaterialInputs inputs, SamplerState materialSampler)
{
    inputs = CalcPixelMaterialInputs(parameters, materialSampler);

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
}

//! Feed to clip(): negative discards.
float GetMaterialMask(PixelMaterialInputs inputs)
{
    return inputs.OpacityMask - kOpacityMaskClipValue;
}

#endif // SPARK_MATERIAL_TEMPLATE_HLSLI
