#include <Shaders/ViewBindings.hlsl>
#include <Shaders/InstanceBindings.hlsl>
#include <Shaders/MaterialBindings.hlsl>

// GBuffer per-pass sampler (space2 = per-pass tier), bound once by GBufferProcessor.
SamplerState g_MatSampler : register(s0, space2);

// Deferred geometry (base) pass. Per-instance model matrix + material index come from
// g_Instances (space4), indexed by INSTANCE_INDEX (per-instance vertex stream). The
// material index selects a record in g_Materials (space3); the material's base color /
// metallic / roughness fill the GBuffer here. DepthPrePass already wrote SceneDepth; this
// pass runs depth-equal and only fills the GBuffer for fragments that survive early-Z.
// Lighting reconstructs world position from SceneDepth, so no position target is written.
struct VSInput
{
    float3 position    : POSITION;        // slot 0, per-vertex
    float3 normal      : NORMAL;          // slot 0
    float4 tangent     : TANGENT;         // slot 0, xyz + .w handedness (MikkTSpace)
    float2 uv          : TEXCOORD0;       // slot 0
    uint   instanceIdx : INSTANCE_INDEX;  // slot 1, per-instance
};

struct VSOutput
{
    float4 position     : SV_Position;
    float3 worldNormal  : NORMAL;
    float4 worldTangent : TANGENT;   // xyz world-space tangent, w = handedness sign
    float2 uv           : TEXCOORD0;
    nointerpolation uint materialIdx : MATERIAL_INDEX;   // per-instance, constant across the triangle
};

struct PSOutput
{
    float4 albedo   : SV_Target0;  // rgb base color
    float4 normal   : SV_Target1;  // xyz world-space normal, stored raw in [-1, 1]
    float4 orm      : SV_Target2;  // r = occlusion, g = roughness, b = metallic
    float4 emissive : SV_Target3;  // rgb HDR emissive, added directly in the lighting pass
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    InstanceData inst = GetInstanceData(input.instanceIdx);
    float4 worldPos = mul(inst.Model, float4(input.position, 1.0));
    output.position = mul(g_ViewProjection, worldPos);
    // Normal uses the inverse-transpose (NormalMatrix); tangent keeps the plain model matrix.
    output.worldNormal = mul((float3x3)inst.NormalMatrix, input.normal);
    output.worldTangent = float4(mul((float3x3)inst.Model, input.tangent.xyz), input.tangent.w);
    output.uv = input.uv;
    output.materialIdx = inst.MaterialIndex;
    return output;
}

PSOutput PSMain(VSOutput input)
{
    PSOutput output;
    MaterialData mat = GetMaterialData(input.materialIdx);
    float3 albedo = mat.BaseColor.rgb;
    uint baseColorTexIndex = mat.TexIndices[SPARK_TEX_SLOT_BASE_COLOR];
    if (baseColorTexIndex != SPARK_INVALID_TEXTURE_INDEX)
    {
        Texture2D<float4> baseColorTex = ResourceDescriptorHeap[NonUniformResourceIndex(baseColorTexIndex)];
        albedo *= baseColorTex.Sample(g_MatSampler, input.uv).rgb;
    }

    float roughness = mat.Roughness;
    float metallic  = mat.Metallic;
    uint MRTexIndex = mat.TexIndices[SPARK_TEX_SLOT_METALLIC_ROUGH];
    if (MRTexIndex != SPARK_INVALID_TEXTURE_INDEX)
    {
        // glTF metallic-roughness: G = roughness, B = metallic. One sample, two channels.
        Texture2D<float4> MRTex = ResourceDescriptorHeap[NonUniformResourceIndex(MRTexIndex)];
        float4 mr = MRTex.Sample(g_MatSampler, input.uv);
        roughness *= mr.g;
        metallic  *= mr.b;
    }

    float occlusion = 1.0;
    uint occlusionTexIndex = mat.TexIndices[SPARK_TEX_SLOT_OCCLUSION];
    if (occlusionTexIndex != SPARK_INVALID_TEXTURE_INDEX)
    {
        Texture2D<float4> occlusionTex = ResourceDescriptorHeap[NonUniformResourceIndex(occlusionTexIndex)];
        occlusion = occlusionTex.Sample(g_MatSampler, input.uv).r;
    }

    float3 N = normalize(input.worldNormal);
    uint normalTexIndex = mat.TexIndices[SPARK_TEX_SLOT_NORMAL];
    if (normalTexIndex != SPARK_INVALID_TEXTURE_INDEX)
    {
        Texture2D<float4> normalTex = ResourceDescriptorHeap[NonUniformResourceIndex(normalTexIndex)];
        float3 nTS = normalTex.Sample(g_MatSampler, input.uv).xyz * 2.0 - 1.0;
        // glTF normalScale scales the tangent XY (bump strength); z stays, final normalize
        // below (via the world-space result) restores unit length.
        nTS.xy *= mat.NormalScale;

        float3 T = normalize(input.worldTangent.xyz - dot(input.worldTangent.xyz, N) * N);
        float3 B = cross(N, T) * input.worldTangent.w;
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(nTS, TBN));
    }

    // Emissive: factor * strength, modulated by the emissive map (sRGB color) when present.
    float3 emissive = mat.Emissive.rgb * mat.Emissive.a;
    uint emissiveTexIndex = mat.TexIndices[SPARK_TEX_SLOT_EMISSIVE];
    if (emissiveTexIndex != SPARK_INVALID_TEXTURE_INDEX)
    {
        Texture2D<float4> emissiveTex = ResourceDescriptorHeap[NonUniformResourceIndex(emissiveTexIndex)];
        emissive *= emissiveTex.Sample(g_MatSampler, input.uv).rgb;
    }

    output.albedo   = float4(albedo, 1.0);
    output.normal   = float4(N, 0.0);
    output.orm      = float4(occlusion, roughness, metallic, 1.0);
    output.emissive = float4(emissive, 1.0);
    return output;
}
