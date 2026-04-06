cbuffer ObjectConstants : register(b0, space0)
{
    float4x4 g_MVP;
};

Texture2D    g_BaseColor : register(t0, space0);
SamplerState g_Sampler   : register(s0, space0);

struct VSInput
{
    float3 position : POSITION;
    float2 uv       : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(g_MVP, float4(input.position, 1.0));
    output.uv = input.uv;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return g_BaseColor.Sample(g_Sampler, input.uv);
}
