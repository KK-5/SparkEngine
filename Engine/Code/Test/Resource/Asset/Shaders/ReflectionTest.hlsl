cbuffer SceneParams : register(b0, space0)
{
    float4x4 viewProj;
    float4   lightDir;
    float    time;
};

Texture2D    g_AlbedoMap  : register(t0, space0);
SamplerState g_AlbedoSamp : register(s0, space0);

struct VSInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(float4(input.position, 1.0), viewProj);
    output.texCoord = input.texCoord;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return g_AlbedoMap.Sample(g_AlbedoSamp, input.texCoord) * time;
}
