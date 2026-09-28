// Completes GBufferPass's Velocity into ResolvedVelocity, the one every temporal consumer reads.
// Pixels no geometry wrote (sky) take the camera's motion, rebuilt from SceneDepth.

#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Lib/Velocity.hlsli>

struct ScopeParameters
{
    uint viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

Texture2D g_Velocity : register(t0, space2);
Texture2D g_Depth    : register(t1, space2);   // SceneDepth, viewed as R32_FLOAT

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    // Full-screen triangle from SV_VertexID: NDC (-1,-1),(3,-1),(-1,3).
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv       = uv;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    int3 px = int3(int2(input.position.xy), 0);   // inputs share this pass's target

    float2 velocity = g_Velocity.Load(px).xy;
    if (!IsVelocityWritten(velocity))
    {
        // The pixel centre is a jittered sample; clipToPrevClip expects an unjittered position.
        const ViewData view = GetView(g_Scope.viewIndex);
        float4 clipPosition = float4(input.uv * 2.0 - 1.0 - view.temporalAAJitter.xy, g_Depth.Load(px).r, 1.0);
        velocity = CalcVelocity(clipPosition, mul(view.clipToPrevClip, clipPosition));
    }
    return float4(velocity, 0.0, 0.0);
}
