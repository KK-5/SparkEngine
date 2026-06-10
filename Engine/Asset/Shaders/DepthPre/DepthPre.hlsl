#include <Shaders/ViewBindings.hlsl>

struct VSInput
{
    float3 position : POSITION;
};

cbuffer ObjectConstants : register(b0, space1)
{
    float4x4 g_Model;
};

float4 VSMain(VSInput input) : SV_Position
{
    return mul(g_ViewProjection, mul(g_Model, float4(input.position, 1.0)));
}

float4 PSMain() : SV_Target0
{
    return float4(0.5, 0.5, 0.5, 1.0);
}