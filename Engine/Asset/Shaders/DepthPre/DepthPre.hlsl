#include <Shaders/ViewBindings.hlsl>
#include <Shaders/InstanceBindings.hlsl>

// Per-instance model matrix comes from the global g_Instances StructuredBuffer
// (space1), indexed by InstanceIdx. InstanceIdx is delivered through a per-instance
// vertex stream (PER_INSTANCE_DATA + StartInstanceLocation), filled by
// InstanceBindingSystem. See TODO_InstanceBindingSystemPlan.md §2.5.
struct VSInput
{
    float3 position    : POSITION;        // slot 0, per-vertex
    uint   instanceIdx : INSTANCE_INDEX;  // slot 1, per-instance
};

struct VSOutput
{
    float4 position : SV_Position;
    float  depth    : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    InstanceData inst = GetInstanceData(input.instanceIdx);
    float4 worldPos = mul(inst.Model, float4(input.position, 1.0));
    output.position = mul(g_ViewProjection, worldPos);
    output.depth    = output.position.z / output.position.w;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    return float4(input.depth, input.depth, input.depth, 1.0);
}
