#include <Shaders/ViewBindings.hlsl>
#include <Shaders/InstanceBindings.hlsl>

// Deferred geometry (base) pass. Per-instance model matrix comes from g_Instances
// (space1), indexed by INSTANCE_INDEX (per-instance vertex stream). DepthPrePass
// already wrote SceneDepth; this pass runs depth-equal and only fills the GBuffer
// for the fragments that survive the early-Z test.
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
    return output;
}

PSOutput PSMain(VSOutput input)
{
    PSOutput output;
    // Hardcoded material until the material system lands: fixed base color, real
    // world normal, fixed roughness/metallic. Lighting samples these three targets.
    output.albedo = float4(0.8, 0.8, 0.8, 1.0);
    output.normal = float4(normalize(input.worldNormal), 0.0);
    output.orm    = float4(1.0, 0.5, 0.0, 1.0);   // ao = 1, roughness = 0.5, metallic = 0
    return output;
}
