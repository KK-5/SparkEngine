// Root constants (space5) beside an ordinary per-pass cbuffer (space2).

struct TestRootConstants
{
    uint   inputIndex;
    uint   outputIndex;
    float2 outputSize;
    float4 tint;
};

[[vk::push_constant]] ConstantBuffer<TestRootConstants> g_Root : register(b0, space5);

cbuffer PassConstants : register(b0, space2)
{
    float g_Exposure;
};

RWTexture2D<float4> g_Output : register(u0, space2);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    g_Output[id.xy] = g_Root.tint * g_Exposure
        + float4(g_Root.outputSize, g_Root.inputIndex, g_Root.outputIndex);
}
