#include <Shaders/ViewBindings.hlsl>
#include <Shaders/InstanceBindings.hlsl>

// Depth pre-pass: writes SceneDepth ONLY — no color target. The rasterizer writes depth
// from SV_Position; the pixel shader outputs nothing. SceneColor is created and owned by
// LightingPass (the first pass that actually produces scene color), not here — this pass
// used to write a depth visualization into SceneColor purely for early validation.
//
// Per-instance model matrix comes from the global g_Instances StructuredBuffer (space1),
// indexed by InstanceIdx, delivered through a per-instance vertex stream filled by
// InstanceBindingSystem. See TODO_InstanceBindingSystemPlan.md §2.5.
struct VSInput
{
    float3 position    : POSITION;        // slot 0, per-vertex
    uint   instanceIdx : INSTANCE_INDEX;  // slot 1, per-instance
};

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    InstanceData inst = GetInstanceData(input.instanceIdx);
    float4 worldPos = mul(inst.Model, float4(input.position, 1.0));
    output.position = mul(g_ViewProjection, worldPos);
    return output;
}

// No render target: the pixel shader writes nothing (depth comes from the rasterizer).
void PSMain(VSOutput input)
{
}
