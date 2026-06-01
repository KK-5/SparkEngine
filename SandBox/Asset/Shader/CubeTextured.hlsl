Texture2D    g_Texture : register(t0, space0);
SamplerState g_Sampler : register(s0, space0);

cbuffer ViewConstants : register(b0, space0)
{
    float4x4 g_ViewProjection;   // per-view, written by Render::View
    float4x4 g_Model;            // per-object, written by the feature
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 tangent  : TANGENT;
    float2 uv       : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(g_ViewProjection, mul(g_Model, float4(input.position, 1.0)));
    output.uv       = input.uv;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return pow(g_Texture.Sample(g_Sampler, input.uv), 1.0 / 2.2);
}
