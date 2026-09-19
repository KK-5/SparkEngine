// Temporal anti-aliasing, after UE4's TemporalAA.usf: reproject last frame's result by the
// closest-depth velocity of the neighbourhood, clip it to the neighbourhood's colour statistics,
// and blend it with this frame's colour reconstructed at the unjittered pixel centre. The output
// is this frame's TemporalAA and next frame's history.
//
// The body is TemporalAA(px), which takes only a pixel coordinate, so a compute entry point
// can wrap it once compute passes enter the graph.

#include <Shaders/ViewBindings.hlsli>

Texture2D<float4> g_SceneColor : register(t0, space2);
Texture2D<float>  g_Depth      : register(t1, space2);   // SceneDepth, viewed as R32_FLOAT
Texture2D<float2> g_Velocity   : register(t2, space2);   // ResolvedVelocity
Texture2D<float4> g_History    : register(t3, space2);   // last frame's TemporalAA

SamplerState g_LinearSampler : register(s0, space2);

cbuffer TemporalAAParams : register(b0, space2)
{
    // 0 when there is no previous frame: its content is undefined, so it must be selected
    // away, never weighted — it may hold NaN.
    uint  g_TemporalAAHistoryValid;
    // From TemporalAAComponent, see there.
    float g_TemporalAACurrentFrameWeight;
    float g_TemporalAAMotionFrameWeight;
    float g_TemporalAAVarianceClipGamma;
    float g_TemporalAAFilterSize;
};

// Motion at which the current frame weight reaches g_TemporalAAMotionFrameWeight: motion makes
// the history less trustworthy, so the current frame takes over faster.
static const float kFastMotionPixels = 8.0;

static const int2 kNeighbourOffsets[9] = {
    int2(-1, -1), int2(0, -1), int2(1, -1),
    int2(-1,  0), int2(0,  0), int2(1,  0),
    int2(-1,  1), int2(0,  1), int2(1,  1),
};

float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// HDR is compressed by luma before any filtering, so a single very bright sample cannot
// dominate the neighbourhood range or the blend (flicker on highlights).
float3 ToPerceptual(float3 c)   { return c * rcp(1.0 + Luma(c)); }
float3 FromPerceptual(float3 c) { return c * rcp(max(1.0 - Luma(c), 1e-4)); }

float3 RGBToYCoCg(float3 c)
{
    return float3(
         0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
         0.5  * c.r             - 0.5  * c.b,
        -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float3 YCoCgToRGB(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Moves history toward the box centre until it lies inside, keeping its hue direction —
// a per-channel clamp would shift colour.
float3 ClipToAABB(float3 history, float3 boxMin, float3 boxMax)
{
    float3 center  = 0.5 * (boxMax + boxMin);
    float3 extents = 0.5 * (boxMax - boxMin) + 1e-5;
    float3 offset  = history - center;
    float3 unit    = abs(offset / extents);
    float  maxUnit = max(unit.x, max(unit.y, unit.z));
    return maxUnit > 1.0 ? center + offset / maxUnit : history;
}

// Gaussian fit of a Blackman-Harris window, distance in pixels scaled by the filter size.
float ReconstructionWeight(float2 offset)
{
    offset /= g_TemporalAAFilterSize;
    return exp(-2.29 * dot(offset, offset));
}

// Catmull-Rom in 5 bilinear taps (the corners of the 4x4 kernel are dropped). Sharper than
// bilinear, which would blur the history a little more every frame.
float3 SampleHistory(float2 uv)
{
    float2 texSize    = g_BufferSizeAndInvSize.xy;
    float2 invTexSize = g_BufferSizeAndInvSize.zw;

    float2 samplePos = uv * texSize;
    float2 texPos1   = floor(samplePos - 0.5) + 0.5;
    float2 f         = samplePos - texPos1;

    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    float2 w12      = w1 + w2;
    float2 texPos0  = (texPos1 - 1.0) * invTexSize;
    float2 texPos3  = (texPos1 + 2.0) * invTexSize;
    float2 texPos12 = (texPos1 + w2 / w12) * invTexSize;

    float4 sum = 0.0;
    sum += float4(g_History.SampleLevel(g_LinearSampler, float2(texPos12.x, texPos0.y),  0).rgb, 1.0) * (w12.x * w0.y);
    sum += float4(g_History.SampleLevel(g_LinearSampler, float2(texPos0.x,  texPos12.y), 0).rgb, 1.0) * (w0.x  * w12.y);
    sum += float4(g_History.SampleLevel(g_LinearSampler, float2(texPos12.x, texPos12.y), 0).rgb, 1.0) * (w12.x * w12.y);
    sum += float4(g_History.SampleLevel(g_LinearSampler, float2(texPos3.x,  texPos12.y), 0).rgb, 1.0) * (w3.x  * w12.y);
    sum += float4(g_History.SampleLevel(g_LinearSampler, float2(texPos12.x, texPos3.y),  0).rgb, 1.0) * (w12.x * w3.y);

    // The negative lobes can undershoot; the clip below bounds it from the other side.
    return max(sum.rgb / sum.w, 0.0);
}

float4 TemporalAA(int2 px)
{
    int2 maxPx = int2(g_BufferSizeAndInvSize.xy) - 1;

    // The scene was shifted by the jitter, so each sample saw the unjittered image at its own
    // centre minus the jitter. In pixels, y down.
    float2 jitterPixels = g_TemporalAAJitter.xy * float2(0.5, -0.5) * g_BufferSizeAndInvSize.xy;

    // Neighbourhood: this frame's colour at the unjittered pixel centre, its colour statistics,
    // and the closest surface's motion. Closest rather than centre, so an edge carries the
    // foreground's motion and does not smear.
    float3 current   = 0.0;
    float  weightSum = 0.0;
    float3 m1        = 0.0;
    float3 m2        = 0.0;
    float3 minColor  = 1e30;
    float3 maxColor  = -1e30;
    float  bestDepth = -1.0;
    int2   bestPx    = px;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        int3 samplePx = int3(clamp(px + kNeighbourOffsets[i], 0, maxPx), 0);

        float3 color = RGBToYCoCg(ToPerceptual(g_SceneColor.Load(samplePx).rgb));

        // Reconstruct rather than take the centre sample: it moves with the jitter, so on its
        // own it changes every frame even when nothing else does.
        float weight = ReconstructionWeight(float2(kNeighbourOffsets[i]) - jitterPixels);
        current   += color * weight;
        weightSum += weight;

        m1 += color;
        m2 += color * color;
        minColor = min(minColor, color);
        maxColor = max(maxColor, color);

        // Reversed-Z: larger is closer.
        float depth = g_Depth.Load(samplePx);
        if (depth > bestDepth)
        {
            bestDepth = depth;
            bestPx    = samplePx.xy;
        }
    }
    current /= weightSum;

    // Variance clipping: a min/max box is stretched by one outlier and collapses when a
    // sub-pixel feature misses the neighbourhood for a frame; mean +- gamma * sigma is not as
    // sensitive to either. Kept inside min/max so it never admits a colour no sample has.
    float3 mean   = m1 / 9.0;
    float3 sigma  = sqrt(max(m2 / 9.0 - mean * mean, 0.0));
    float3 boxMin = max(mean - g_TemporalAAVarianceClipGamma * sigma, minColor);
    float3 boxMax = min(mean + g_TemporalAAVarianceClipGamma * sigma, maxColor);

    // Velocity is an NDC delta; NDC y points up, UV y down.
    float2 velocity = g_Velocity.Load(int3(bestPx, 0));
    float2 uv       = (float2(px) + 0.5) * g_BufferSizeAndInvSize.zw;
    float2 prevUV   = uv - velocity * float2(0.5, -0.5);

    bool onScreen     = all(prevUV >= 0.0) && all(prevUV <= 1.0);
    bool historyValid = g_TemporalAAHistoryValid != 0 && onScreen;

    float3 history = RGBToYCoCg(ToPerceptual(SampleHistory(prevUV)));
    history = ClipToAABB(history, boxMin, boxMax);

    float motionPixels = length(velocity * 0.5 * g_BufferSizeAndInvSize.xy);
    float blend        = lerp(g_TemporalAACurrentFrameWeight, g_TemporalAAMotionFrameWeight, saturate(motionPixels / kFastMotionPixels));
    float3 blended     = lerp(history, current, blend);

    // Select, not weight: an invalid history may be NaN, and 0 * NaN is NaN.
    float3 result = historyValid ? blended : current;
    result = FromPerceptual(YCoCgToRGB(result));
    result = any(isnan(result)) || any(isinf(result)) ? 0.0 : result;
    return float4(result, 1.0);
}

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    // Full-screen triangle from SV_VertexID: NDC (-1,-1),(3,-1),(-1,3).
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    // Inputs share this pass's target, so the pixel addresses them directly.
    return TemporalAA(int2(input.position.xy));
}
