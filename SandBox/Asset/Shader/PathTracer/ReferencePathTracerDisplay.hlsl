// Presentation only: accumulation stores linear scene radiance, so exposure,
// tone mapping and display encoding can change without invalidating the mean.
#include <Shaders/ViewBindings.hlsli>

struct ScopeParameters
{
    uint viewIndex;
    uint debugMode;
};
#include <Shaders/ScopeBindings.hlsli>

Texture2D<float4> g_Accumulation : register(t0, space2);

struct VSOutput
{
    float4 Position : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.Position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float3 LinearToDisplay(float3 color)
{
    color = max(color, 0.0);
    return lerp(1.055 * pow(color, 1.0 / 2.4) - 0.055, 12.92 * color, color <= 0.0031308);
}

float4 PSMain(VSOutput input) : SV_Target0
{
    float3 color = g_Accumulation.Load(int3(int2(input.Position.xy), 0)).rgb;
    if (g_Scope.debugMode == 0u || g_Scope.debugMode == 6u)
    {
        const ViewData view = GetView(g_Scope.viewIndex);
        color = max(color * view.exposure, 0.0);
        color = LinearToDisplay(color / (1.0 + color));
    }
    return float4(color, 1.0);
}
