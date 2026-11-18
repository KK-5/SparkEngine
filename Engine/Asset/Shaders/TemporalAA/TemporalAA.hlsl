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

static const float kMaxEdgeClipRelaxation = 0.25;
static const float kEdgeClipMotionLimit = 0.5; // pixels per frame

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
    return color * rcp(1.0 - Luma(color));
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
    return ToPerceptual(g_SceneColor.Load(int3(px, 0)).rgb);
}

// Quality-mode clipping towards the current colour. Clamping the target handles
// current-colour outliers outside mean +/- sigma. For an outside component, the
// direction towards this target is necessarily nonzero, even at zero variance.
float3 ClipHistoryColor(float3 history, float3 current, float3 mean, float3 sigma, float gamma)
{
    const float3 extents = gamma * sigma;
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
            entry = max(entry, (boxMin[axis] - history[axis]) / direction[axis]);
        }
        else if (history[axis] > boxMax[axis])
        {
            entry = max(entry, (boxMax[axis] - history[axis]) / direction[axis]);
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
// Alpha is sampled along with RGB, as in the reference accumulation path.
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
    // GBuffer/VelocityResolve provide valid depth and finite depth motion. Read
    // their invalid-motion sentinel and A flag instead of revalidating the data.
    return IsTemporalMotionValid(surface.Velocity.xy)
        && (surface.DeviceZ == 0.0 || surface.Velocity.w > 0.5);
}

float ExpectedPreviousViewZ(ViewData view, TemporalSurface surface, float previousFarViewZ)
{
    // Sky carries rotation-only RG, with no finite depth motion in B/A.
    return surface.DeviceZ == 0.0
        ? previousFarViewZ
        : ConvertFromDeviceZ(view, surface.DeviceZ) + surface.Velocity.z;
}

// Intel's nearest-current / farthest-previous depth envelope, adapted to our B
// depth motion. Each current sample predicts its own previous Z before taking
// the minimum. Both 2x2 footprints follow the unjittered output/history grid,
// comparing local coverage rather than the jittered centre's surface identity.
bool ValidateHistoryDepth(ViewData view, TemporalSurface surface, float2 historyUV,
                          int2 minPx, int2 maxPx, out float edgeStrength)
{
    edgeStrength = 0.0;
    const float2 outputUV = TemporalBufferPixelToViewUV(view, float2(surface.Pixel) + 0.5);
    const float2 currentDepthUV = outputUV + TemporalNDCToUVDelta(view.temporalAAJitter.xy);
    const float2 previousDepthUV = historyUV + TemporalNDCToUVDelta(view.temporalAAJitter.zw);
    if (!IsTemporalViewUVValid(previousDepthUV))
    {
        return false;
    }

    // MiniEngine's bias is in viewZ / farZ. The main view has a finite far clip.
    // Sky uses this same far depth instead of a separate point-validity test.
    const float previousFarViewZ = ConvertFromDeviceZ(view.prevInvDeviceZToViewZ, 0.0);
    const float2 currentPosition = view.viewRectMin.xy + currentDepthUV * view.viewSizeAndInvSize.xy;
    const int2 currentBasePx = int2(floor(currentPosition - 0.5));

    // The centre is always inside this 2x2 footprint for our +/- half-pixel
    // jitter, and the caller has checked its motion validity. It supplies the
    // initial candidate; invalid neighbours cannot contribute depth motion.
    float nearestViewZ = ExpectedPreviousViewZ(view, surface, previousFarViewZ);
    float farthestPredictedViewZ = nearestViewZ;
    float nearestDepthError = surface.DeviceZ == 0.0 ? 0.0 : VelocityDepthQuantizationError(surface.Velocity.z);
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            const int2 samplePx = clamp(currentBasePx + int2(x, y), minPx, maxPx);
            if (any(samplePx != surface.Pixel))
            {
                TemporalSurface candidate;
                candidate.Pixel = samplePx;
                candidate.DeviceZ = g_Depth.Load(int3(samplePx, 0));
                candidate.Velocity = g_Velocity.Load(int3(samplePx, 0));
                if (IsSurfaceMotionValid(candidate))
                {
                    const float expectedViewZ = ExpectedPreviousViewZ(view, candidate, previousFarViewZ);
                    farthestPredictedViewZ = max(farthestPredictedViewZ, expectedViewZ);
                    if (expectedViewZ < nearestViewZ)
                    {
                        nearestViewZ = expectedViewZ;
                        nearestDepthError = candidate.DeviceZ == 0.0
                            ? 0.0 : VelocityDepthQuantizationError(candidate.Velocity.z);
                    }
                }
            }
        }
    }

    const float2 previousPosition = view.viewRectMin.xy + previousDepthUV * view.viewSizeAndInvSize.xy;
    const int2 previousBasePx = int2(floor(previousPosition - 0.5));
    float nearestHistoryViewZ = previousFarViewZ;
    float farthestViewZ = 0.0;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            // Match the reference clamp sampler within this view's rectangle.
            const int2 samplePx = clamp(previousBasePx + int2(x, y), minPx, maxPx);
            const float previousDeviceZ = g_PreviousDepth.Load(int3(samplePx, 0));
            // Reversed device Z=0 maps to the finite far clip and participates
            // in the envelope, matching the reference normalized depth buffer.
            const float previousViewZ = ConvertFromDeviceZ(view.prevInvDeviceZToViewZ, previousDeviceZ);
            nearestHistoryViewZ = min(nearestHistoryViewZ, previousViewZ);
            farthestViewZ = max(farthestViewZ, previousViewZ);
        }
    }

    const float bias = g_TemporalAADepthRejectionBias * previousFarViewZ + nearestDepthError;

    // The edge can be missing from either footprint in one jitter phase.
    // Measure the relative depth span on both frames to keep it detectable.
    // The predicted depth can round to zero when a large FP16 B cancels viewZ.
    const float currentSpan = farthestPredictedViewZ > 0.0
        ? (farthestPredictedViewZ - nearestViewZ) / farthestPredictedViewZ : 0.0;
    const float historySpan = 1.0 - nearestHistoryViewZ / farthestViewZ;
    edgeStrength = smoothstep(0.02, 0.10, max(currentSpan, historySpan));
    return nearestViewZ <= farthestViewZ + bias;
}

float4 EncodeResult(float3 perceptualColor, float historyWeight)
{
    return float4(FromPerceptual(perceptualColor), historyWeight);
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
    const float2 centerMotionPixels = TemporalNDCToUVDelta(surface.Velocity.xy) * view.viewSizeAndInvSize.xy;
    float longestMotionSquared = centerMotionValid
        ? dot(centerMotionPixels, centerMotionPixels) : 0.0;
    float neighbourhoodMotionSquared = longestMotionSquared;

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
            if (IsSurfaceMotionValid(candidate))
            {
                const float2 motionPixels = TemporalNDCToUVDelta(candidate.Velocity.xy) * view.viewSizeAndInvSize.xy;
                const float motionSquared = dot(motionPixels, motionPixels);
                // Motion on either side of the edge should reduce relaxation.
                neighbourhoodMotionSquared = max(neighbourhoodMotionSquared, motionSquared);
                if ((candidate.DeviceZ == 0.0) == (surface.DeviceZ == 0.0) && motionSquared > longestMotionSquared)
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
    const float motionConfidence = saturate(1.0 - sqrt(longestMotionSquared) / motionLimit);
    const float2 viewUV = TemporalBufferPixelToViewUV(view, float2(px) + 0.5);
    const float2 historyUV = ReprojectTemporalHistoryUV(viewUV, motionSurface.Velocity.xy);
    bool historyValid = g_TemporalAAHistoryValid != 0 && g_PreviousDepthValid != 0
                     && view.temporalHistoryValid != 0 && centerMotionValid
                     && motionConfidence > 0.0 && IsTemporalViewUVValid(historyUV);
    float edgeStrength = 0.0;
    [branch]
    if (historyValid)
    {
        historyValid = ValidateHistoryDepth(view, surface, historyUV, minPx, maxPx, edgeStrength);
    }
    [branch]
    if (!historyValid)
    {
        return EncodeResult(fallback, 0.5);
    }

    const float4 rawHistory = SampleHistory(view, historyUV, minPx, maxPx);
    const float3 mean = moment1 * (1.0 / 9.0);
    const float3 sigma = sqrt(max(moment2 * (1.0 / 9.0) - mean * mean, 0.0));
    const float gamma = lerp(g_TemporalAAMinVarianceGamma, g_TemporalAAMaxVarianceGamma,
                             motionConfidence * motionConfidence);
    const float3 history = ToPerceptual(rawHistory.rgb);
    const float3 clippedHistory = YCoCgToRGB(ClipHistoryColor(
        RGBToYCoCg(history), RGBToYCoCg(current), mean, sigma, gamma));
    const float stillness = 1.0 - smoothstep(0.0, kEdgeClipMotionLimit * kEdgeClipMotionLimit, neighbourhoodMotionSquared);
    const float clipRelaxation = kMaxEdgeClipRelaxation * edgeStrength * stillness;
    const float3 relaxedHistory = lerp(clippedHistory, history, clipRelaxation);
    const float weight = rawHistory.a * motionConfidence;
    const float nextWeight = saturate(rcp(2.0 - weight));
    return EncodeResult(lerp(current, relaxedHistory, weight), nextWeight);
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
