cbuffer ViewConstants : register(b0, space0)
{
    float4x4 g_MVP;
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

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(g_MVP, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(input.color, 1.0);
}
