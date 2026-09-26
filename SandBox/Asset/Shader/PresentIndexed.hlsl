// Full-screen copy of a render graph image the pass reaches by heap index.

struct PresentRootConstants
{
    uint inputIndex;
};

[[vk::push_constant]] ConstantBuffer<PresentRootConstants> g_Root : register(b0, space5);

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(uint vertexId : SV_VertexID)
{
    // One triangle covering the screen: (-1,-1), (3,-1), (-1,3).
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    PSInput output;
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    Texture2D<float4> source = ResourceDescriptorHeap[g_Root.inputIndex];
    return source.Load(int3(input.position.xy, 0));
}
