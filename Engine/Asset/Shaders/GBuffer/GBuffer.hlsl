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
    float2 uv          : TEXCOORD0;       // slot 0
    uint   instanceIdx : INSTANCE_INDEX;  // slot 1, per-instance
};

struct VSOutput
{
    float4 position    : SV_Position;
    float3 worldNormal : NORMAL;
    float2 uv          : TEXCOORD0;
    nointerpolation uint materialIdx : MATERIAL_INDEX;   // per-instance, constant across the triangle
};

struct PSOutput
{
    float4 albedo : SV_Target0;  // rgb base color
    float4 normal : SV_Target1;  // xyz world-space normal, stored raw in [-1, 1]
    float4 orm    : SV_Target2;  // r = occlusion, g = roughness, b = metallic
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    InstanceData inst = GetInstanceData(input.instanceIdx);
    float4 worldPos = mul(inst.Model, float4(input.position, 1.0));
    output.position = mul(g_ViewProjection, worldPos);
    // Upper 3x3 assumes uniform scale; non-uniform scale would need the
    // inverse-transpose (revisit when the material/transform path formalizes).
    output.worldNormal = mul((float3x3)inst.Model, input.normal);
    output.uv = input.uv;
    output.materialIdx = inst.MaterialIndex;
    return output;
}

PSOutput PSMain(VSOutput input)
{
    PSOutput output;
    // Material-driven base color / roughness / metallic (replaces the old hardcoded
    // values). Occlusion stays 1 for now; specular is carried in g_Materials but not
    // yet routed into the GBuffer (no F0 channel yet — a later step).
    MaterialData mat = GetMaterialData(input.materialIdx);
    float3 albedo = mat.BaseColor.rgb;
    uint baseColorTexIndex = mat.TexIndices[SPARK_TEX_SLOT_BASE_COLOR];
    if (baseColorTexIndex != SPARK_INVALID_TEXTURE_INDEX)
    {
        Texture2D<float4> baseColorTex = ResourceDescriptorHeap[NonUniformResourceIndex(baseColorTexIndex)];
        albedo *= baseColorTex.Sample(g_MatSampler, input.uv).rgb;
    }
    output.albedo = float4(albedo, 1.0);
    output.normal = float4(normalize(input.worldNormal), 0.0);
    output.orm    = float4(1.0, mat.Roughness, mat.Metallic, 1.0);
    return output;
}
