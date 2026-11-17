// Adapted from Intel Graphics Optimized TAA, based on Microsoft's MiniEngine.
// https://github.com/GameTechDev/TAA/blob/39786709cf70a1e0906196c600f6079571a33ceb/MiniEngine/Core/Shaders/TAAResolve.hlsl
// Quality path: 9-sample variance clipping, longest velocity, 5-tap bicubic,
// neighbourhood depth rejection and recursive history weight. Adaptations are
// documented in Document/TODO_TemporalHistoryValidationPlan.md, section 7.
//
// Copyright (c) 2019-2020, Intel Corporation
// Copyright (c) 2013-2015 Microsoft
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <Shaders/Lib/Temporal/TemporalReprojection.hlsli>

struct ScopeParameters
{
    uint viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

Texture2D<float4> g_SceneColor    : register(t0, space2);
Texture2D<float>  g_Depth         : register(t1, space2);
Texture2D<float4> g_Velocity      : register(t2, space2); // NDC XY, previousViewZ - currentViewZ, validity
Texture2D<float4> g_History       : register(t3, space2); // linear HDR RGB, recursive history weight A
Texture2D<float>  g_PreviousDepth : register(t4, space2); // previous jittered, reversed device Z

SamplerState g_LinearSampler : register(s0, space2);

cbuffer TemporalAAParams : register(b0, space2)
{
    uint  g_TemporalAAHistoryValid;
    uint  g_PreviousDepthValid;
    float g_TemporalAAMinVarianceGamma;
    float g_TemporalAAMaxVarianceGamma;
    float g_TemporalAAMotionRejectionPixels; // pixels/frame at 1080p
    float g_TemporalAADepthRejectionBias;   // fraction of the previous far viewZ, as in MiniEngine
};

static const int2 kNeighbourOffsets[9] = {
    int2(-1, -1), int2(0, -1), int2(1, -1),
    int2(-1,  0), int2(0,  0), int2(1,  0),
    int2(-1,  1), int2(0,  1), int2(1,  1),
};

// Keep a current-frame contribution even if an old history contained alpha=1,
// or bicubic's negative lobes overshoot. New histories start at 0.5.
static const float kHistoryWeightLimit = 1.0 - 1.0 / 1024.0;
static const float kMaxHDR = 65504.0; // output is RGBA16F

float Luma(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 ToPerceptual(float3 color)
{
    return color * rcp(1.0 + Luma(color));
}

float3 FromPerceptual(float3 color)
{
    return color * rcp(max(1.0 - Luma(color), 1e-6));
}

float3 RGBToYCoCg(float3 color)
{
    return float3(
         0.25 * color.r + 0.5 * color.g + 0.25 * color.b,
         0.5  * color.r                 - 0.5 * color.b,
        -0.25 * color.r + 0.5 * color.g - 0.25 * color.b);
}

float3 YCoCgToRGB(float3 color)
{
    return float3(color.x + color.y - color.z, color.x + color.z, color.x - color.y - color.z);
}

float3 LoadCurrentColor(int2 px)
{
    const float3 color = g_SceneColor.Load(int3(px, 0)).rgb;
    return all(isfinite(color)) ? ToPerceptual(clamp(color, 0.0, kMaxHDR)) : 0.0;
}

// Quality-mode clipping towards the current colour. Intersect all slabs rather
// than dividing by a zero direction or stopping at a slab while still outside
// another. Clamping the target handles current-colour outliers outside mean +/- sigma.
float3 ClipHistoryColor(float3 history, float3 current, float3 mean, float3 sigma, float gamma)
{
    const float3 extents = max(gamma * sigma, 1e-6);
    const float3 boxMin = mean - extents;
    const float3 boxMax = mean + extents;
    const float3 target = clamp(current, boxMin, boxMax);
    const float3 direction = target - history;
    float entry = 0.0;
    [unroll]
    for (int axis = 0; axis < 3; ++axis)
    {
        if (history[axis] < boxMin[axis])
        {
            entry = max(entry, (boxMin[axis] - history[axis]) / max(direction[axis], 1e-8));
        }
        else if (history[axis] > boxMax[axis])
        {
            entry = max(entry, (boxMax[axis] - history[axis]) / min(direction[axis], -1e-8));
        }
    }
    return lerp(history, target, saturate(entry));
}

float4 SampleHistoryTap(ViewData view, float2 pixelPosition, int2 minPx, int2 maxPx)
{
    // Replicate the view's edge, not a neighbouring view or unused buffer area.
    const float2 uv = clamp(pixelPosition, float2(minPx) + 0.5, float2(maxPx) + 0.5)
                    * view.bufferSizeAndInvSize.zw;
    return g_History.SampleLevel(g_LinearSampler, uv, 0);
}

// Intel's BicubicSampling5, including its approximated corner contributions.
// RGB stays linear HDR in storage so Tonemap, Bloom and SSR keep their contract.
// Alpha is sampled along with RGB, then bounded before use as a weight.
float4 SampleHistory(ViewData view, float2 viewUV, int2 minPx, int2 maxPx)
{
    const float2 samplePosition = view.viewRectMin.xy + viewUV * view.viewSizeAndInvSize.xy;
    const float2 basePosition = floor(samplePosition - 0.5) + 0.5;
    const float2 f = samplePosition - basePosition;
    const float2 f2 = f * f;
    const float2 f3 = f2 * f;
    const float2 w0 = -0.5 * f3 + f2 - 0.5 * f;
    const float2 w1 =  1.5 * f3 - 2.5 * f2 + 1.0;
    const float2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
    const float2 w3 =  0.5 * f3 - 0.5 * f2;
    const float2 w12 = w1 + w2;
    const float2 middle = basePosition + w2 / w12;

    const float4 top    = SampleHistoryTap(view, float2(middle.x, basePosition.y - 1.0), minPx, maxPx);
    const float4 left   = SampleHistoryTap(view, float2(basePosition.x - 1.0, middle.y), minPx, maxPx);
    const float4 center = SampleHistoryTap(view, middle, minPx, maxPx);
    const float4 right  = SampleHistoryTap(view, float2(basePosition.x + 2.0, middle.y), minPx, maxPx);
    const float4 bottom = SampleHistoryTap(view, float2(middle.x, basePosition.y + 2.0), minPx, maxPx);

    return (0.5 * (top + left) * w0.x + top * w12.x + 0.5 * (top + left) * w3.x) * w0.y
         + (left * w0.x + center * w12.x + right * w3.x) * w12.y
         + (0.5 * (left + bottom) * w0.x + bottom * w12.x + 0.5 * (right + bottom) * w3.x) * w3.y;
}

struct TemporalSurface
{
    int2 Pixel;
    float DeviceZ;
    float4 Velocity;
};

bool IsSurfaceMotionValid(TemporalSurface surface)
{
    return isfinite(surface.DeviceZ) && surface.DeviceZ >= 0.0 && surface.DeviceZ <= 1.0
        && IsTemporalMotionValid(surface.Velocity.xy)
        && (surface.DeviceZ == 0.0 || IsVelocityDepthValid(surface.Velocity));
}

// Intel uses a one-sided occlusion test against the farthest of four previous
// linear depths. Unlike an absolute difference against one texel, a slope does
// not reject history simply because reprojection falls between pixel centres.
// Each invocation keeps the surface's own pixel, depth and complete motion record.
bool ValidateHistoryDepth(ViewData view, TemporalSurface surface, int2 minPx, int2 maxPx)
{
    if (!IsSurfaceMotionValid(surface))
    {
        return false;
    }
    const float2 rasterUV = TemporalBufferPixelToViewUV(view, float2(surface.Pixel) + 0.5);
    const float2 previousDepthUV = ReprojectTemporalDepthUV(view, rasterUV, surface.Velocity.xy);
    if (!IsTemporalViewUVValid(previousDepthUV))
    {
        return false;
    }
    const float2 previousPosition = view.viewRectMin.xy + previousDepthUV * view.viewSizeAndInvSize.xy;
    if (surface.DeviceZ == 0.0)
    {
        // Sky has no finite depth motion. Do not turn a geometry tap into sky
        // merely because another tap in its neighbourhood contains background.
        const int2 previousPx = int2(floor(previousPosition));
        return g_PreviousDepth.Load(int3(previousPx, 0)) == 0.0;
    }

    const float expectedViewZ = ConvertFromDeviceZ(view, surface.DeviceZ) + surface.Velocity.z;
    if (!isfinite(expectedViewZ) || expectedViewZ <= 0.0)
    {
        return false;
    }

    const int2 basePx = int2(floor(previousPosition - 0.5));
    float farthestViewZ = 0.0;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            const int2 samplePx = basePx + int2(x, y);
            if (all(samplePx >= minPx) && all(samplePx <= maxPx))
            {
                const float previousDeviceZ = g_PreviousDepth.Load(int3(samplePx, 0));
                // An empty footprint cannot validate a surface. Background is
                // not an infinite far sample that accepts every geometry point.
                if (isfinite(previousDeviceZ) && previousDeviceZ > 0.0 && previousDeviceZ <= 1.0)
                {
                    const float previousViewZ = ConvertFromDeviceZ(view.prevInvDeviceZToViewZ, previousDeviceZ);
                    if (isfinite(previousViewZ) && previousViewZ > 0.0)
                    {
                        farthestViewZ = max(farthestViewZ, previousViewZ);
                    }
                }
            }
        }
    }
    if (farthestViewZ <= 0.0)
    {
        return false;
    }

    // MiniEngine stores linear depth as viewZ / farZ (LinearizeDepthCS.hlsl).
    // Convert its normalized bias into our positive world-unit viewZ. Infinite
    // far projections have no finite normalization: use relative view depth there.
    const float farViewZ = ConvertFromDeviceZ(view.prevInvDeviceZToViewZ, 0.0);
    const float biasScale = isfinite(farViewZ) && farViewZ > 0.0 ? farViewZ : expectedViewZ;
    const float bias = g_TemporalAADepthRejectionBias * biasScale
                     + VelocityDepthQuantizationError(surface.Velocity.z);
    return expectedViewZ <= farthestViewZ + bias;
}

float4 EncodeResult(float3 perceptualColor, float historyWeight)
{
    const float3 color = FromPerceptual(max(perceptualColor, 0.0));
    return float4(all(isfinite(color)) ? clamp(color, 0.0, kMaxHDR) : 0.0, historyWeight);
}

float4 TemporalAA(int2 px)
{
    const ViewData view = GetView(g_Scope.viewIndex);
    const int2 minPx = int2(view.viewRectMin.xy);
    const int2 maxPx = min(minPx + int2(view.viewSizeAndInvSize.xy), int2(view.bufferSizeAndInvSize.xy)) - 1;

    TemporalSurface surface;
    surface.Pixel = px;
    surface.DeviceZ = g_Depth.Load(int3(px, 0));
    surface.Velocity = g_Velocity.Load(int3(px, 0));
    TemporalSurface motionSurface = surface;
    const bool centerMotionValid = IsSurfaceMotionValid(surface);
    float longestMotionSquared = centerMotionValid
        ? dot(TemporalNDCToUVDelta(surface.Velocity.xy) * view.viewSizeAndInvSize.xy,
              TemporalNDCToUVDelta(surface.Velocity.xy) * view.viewSizeAndInvSize.xy) : 0.0;

    const float3 current = LoadCurrentColor(px);
    float3 moment1 = 0.0;
    float3 moment2 = 0.0;
    float3 fallback = current;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        const int2 samplePx = clamp(px + kNeighbourOffsets[i], minPx, maxPx);
        const float3 color = i == 4 ? current : LoadCurrentColor(samplePx);
        const float3 colorYCoCg = RGBToYCoCg(color);
        moment1 += colorYCoCg;
        moment2 += colorYCoCg * colorYCoCg;
        if (all(kNeighbourOffsets[i] != 0))
        {
            fallback += color;
        }

        if (centerMotionValid && i != 4)
        {
            TemporalSurface candidate;
            candidate.Pixel = samplePx;
            candidate.DeviceZ = g_Depth.Load(int3(samplePx, 0));
            candidate.Velocity = g_Velocity.Load(int3(samplePx, 0));
            // Never borrow valid history for a first-appearance/reset centre,
            // or cross the finite-surface / infinite-sky boundary.
            if (IsSurfaceMotionValid(candidate) && (candidate.DeviceZ == 0.0) == (surface.DeviceZ == 0.0))
            {
                const float2 motionPixels = TemporalNDCToUVDelta(candidate.Velocity.xy) * view.viewSizeAndInvSize.xy;
                const float motionSquared = dot(motionPixels, motionPixels);
                if (motionSquared > longestMotionSquared)
                {
                    motionSurface = candidate;
                    longestMotionSquared = motionSquared;
                }
            }
        }
    }
    // Reference no-history cross/diagonal filter only; valid pixels use the raw
    // centre sample, with no extra per-frame Gaussian reconstruction blur.
    fallback *= 0.2;

    const float motionLimit = g_TemporalAAMotionRejectionPixels * (view.viewSizeAndInvSize.y / 1080.0);
    const float motionConfidence = saturate(1.0 - sqrt(longestMotionSquared) / max(motionLimit, 1e-4));
    const float2 viewUV = TemporalBufferPixelToViewUV(view, float2(px) + 0.5);
    const float2 historyUV = ReprojectTemporalHistoryUV(viewUV, motionSurface.Velocity.xy);
    bool historyValid = g_TemporalAAHistoryValid != 0 && g_PreviousDepthValid != 0
                     && view.temporalHistoryValid != 0 && centerMotionValid
                     && motionConfidence > 0.0 && IsTemporalViewUVValid(historyUV);
    [branch]
    if (historyValid)
    {
        // Centre validation protects newly revealed background from a dilated
        // foreground vector. The chosen representative also validates its own
        // depth and B; this remains a coverage heuristic, not a per-colour-tap proof.
        historyValid = ValidateHistoryDepth(view, surface, minPx, maxPx);
        if (historyValid && any(motionSurface.Pixel != surface.Pixel))
        {
            historyValid = ValidateHistoryDepth(view, motionSurface, minPx, maxPx);
        }
    }
    [branch]
    if (!historyValid)
    {
        return EncodeResult(fallback, 0.5);
    }

    const float4 rawHistory = SampleHistory(view, historyUV, minPx, maxPx);
    if (!all(isfinite(rawHistory)))
    {
        return EncodeResult(fallback, 0.5);
    }
    const float3 mean = moment1 * (1.0 / 9.0);
    const float3 sigma = sqrt(max(moment2 * (1.0 / 9.0) - mean * mean, 0.0));
    const float gamma = lerp(g_TemporalAAMinVarianceGamma, g_TemporalAAMaxVarianceGamma,
                             motionConfidence * motionConfidence);
    const float3 history = ToPerceptual(clamp(rawHistory.rgb, 0.0, kMaxHDR));
    const float3 clippedHistory = YCoCgToRGB(ClipHistoryColor(
        RGBToYCoCg(history), RGBToYCoCg(current), mean, sigma, gamma));
    const float weight = clamp(rawHistory.a, 0.0, kHistoryWeightLimit) * motionConfidence;
    const float nextWeight = rcp(2.0 - weight);
    return EncodeResult(lerp(current, clippedHistory, weight), nextWeight);
}

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    return TemporalAA(int2(input.position.xy));
}
