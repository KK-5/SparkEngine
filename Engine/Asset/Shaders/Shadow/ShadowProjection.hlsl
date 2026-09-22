// Projects the shadow atlas into screen space: one full-screen draw, one instance per
// ShadowMask slice, each writing the four lights packed into that slice.
//
// No depth attachment, so the sky is shaded too and its mask values are meaningless. It
// cannot have one: SceneDepth is a plain 2D image while ShadowMask is an array, and a
// render pass renders one layer count for every attachment. Nothing reads sky anyway --
// the lighting passes depth-test it away themselves.

#include <Shaders/ViewBindings.hlsli>
#include <Shaders/SceneBindings.hlsli>
#include <Shaders/Lib/DeferredShadingCommon.hlsli>   // DecodeNormal
#include <Shaders/Lib/Shadow/ShadowMask.hlsli>
#include <Shaders/Lib/Shadow/ShadowSampling.hlsli>

Texture2D              g_GBufferNormal : register(t0, space2);
Texture2D              g_Depth         : register(t1, space2);   // SceneDepth as R32_FLOAT
Texture2D              g_ShadowAtlas   : register(t2, space2);   // atlas as R32_FLOAT
SamplerComparisonState g_ShadowSampler : register(s0, space2);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    uint   slice    : SV_RenderTargetArrayIndex;
};

VSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv       = uv;
    output.slice    = instanceId;
    return output;
}

float3 ReconstructWorldPos(float2 uv, float depth)
{
    float2 ndc = uv * 2.0 - 1.0;
    float4 worldH = mul(g_InvViewProj, float4(ndc, depth, 1.0));
    return worldH.xyz / worldH.w;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    int3 px = int3(int2(input.position.xy), 0);

    float3 worldPos = ReconstructWorldPos(input.uv, g_Depth.Load(px).r);
    float3 N = normalize(DecodeNormal(g_GBufferNormal.Load(px).xyz));

    // Unwritten channels read as lit, so a slice holding fewer than four lights is correct
    // without the consumer knowing how many it holds.
    float4 mask = float4(1.0, 1.0, 1.0, 1.0);

    // One sweep of the light list rather than a slot-to-light table: at these light counts
    // it costs what the lighting loop already costs, and it needs no extra buffer.
    for (uint i = 0; i < g_LightCount; ++i)
    {
        LightData light = GetLight(i);
        if (light.shadowMaskIndex < 0 || ShadowMaskSlice(light.shadowMaskIndex) != input.slice)
        {
            continue;
        }

        int row = light.shadowIndex;
        if (light.shadowFaceCount > 1)
        {
            row += int(CubeFaceIndex(worldPos - light.position));
        }
        mask[ShadowMaskChannel(light.shadowMaskIndex)] =
            SampleShadow(GetShadowView(row), worldPos, N,
                         g_ShadowAtlas, g_ShadowSampler, g_ShadowAtlasTexelSize);
    }
    return mask;
}
