// A float3 root constant: DXIL and SPIR-V may pack it differently, so reflection rejects it.

struct TestRootConstants
{
    float3 direction;
    float  scale;
};

[[vk::push_constant]] ConstantBuffer<TestRootConstants> g_Root : register(b0, space5);

RWTexture2D<float4> g_Output : register(u0, space2);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    g_Output[id.xy] = float4(g_Root.direction * g_Root.scale, 1.0);
}
