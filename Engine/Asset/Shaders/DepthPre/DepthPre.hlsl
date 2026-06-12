#include <Shaders/ViewBindings.hlsl>

// Per-object model matrix, space1. Filled by DepthPreProcessor from
// Transform::WorldTransformMatrix (identity if the entity has no transform).
cbuffer ModelBindings : register(b0, space1)
{
    float4x4 g_Model;
};

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float  depth    : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(g_Model, float4(input.position, 1.0));
    output.position = mul(g_ViewProjection, worldPos);
    output.depth    = output.position.z / output.position.w;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    return float4(input.depth, input.depth, input.depth, 1.0);
}