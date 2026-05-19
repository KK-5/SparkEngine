cbuffer ViewConstants : register(b0, space0)
{
    float4x4 g_MVP;
    float3   g_Colors[3];
};

struct VSInput
{
    float3 position : POSITION;
    float3 color    : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
};

PSInput VSMain(VSInput input, uint vertexId : SV_VertexID)
{
    PSInput output;
    // output.position = mul(g_MVP, float4(input.position, 1.0));
    output.position = float4(input.position, 1.0);
    //output.color = input.color;
    output.color = g_Colors[vertexId];
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(input.color, 1.0);
}
