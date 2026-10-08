#ifndef SPARK_LIB_XEGTAO_HLSLI
#define SPARK_LIB_XEGTAO_HLSLI

// XeGTAO: ground-truth ambient occlusion (Jimenez et al., "Practical Real-Time Strategies for
// Accurate Indirect Occlusion") as implemented by Intel. Ported from XeGTAO.h, XeGTAO.hlsli and
// vaGTAO.hlsl of https://github.com/GameTechDev/XeGTAO, version 1.30, by Filip Strugar and
// Steve Mccalla:
//
//   Copyright (C) 2016-2021, Intel Corporation
//   SPDX-License-Identifier: MIT
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this
//   software and associated documentation files (the "Software"), to deal in the Software
//   without restriction, including without limitation the rights to use, copy, modify, merge,
//   publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
//   to whom the Software is furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all copies or
//   substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
//   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
//   PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
//   FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
//   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//   DEALINGS IN THE SOFTWARE.
//
// Three steps, each a compute pass of its own (Shaders/AmbientOcclusion/): PrefilterDepths
// turns device depth into a view-space depth mip chain, MainPass integrates the visibility of
// each pixel, Denoise blurs it without crossing depth edges.
//
// View space here is x right, y up, z forward (positive depth), and screen positions are
// [0, 1] with y down: the source's convention, and the engine's left-handed view space.
//
// Changes from the source:
// - The C++ half of XeGTAO.h is left out: the passes' shaders fill GTAOConstants themselves.
// - Depths are 32-bit float, so the half-precision path is gone and lpfloat is plain float.
// - The working term and the edges are stored in float textures rather than R8_UINT and
//   R8_UNORM, neither of which every backend can write as a storage image. The term is no
//   longer packed (no XE_GTAO_OCCLUSION_TERM_SCALE); the edges keep their 2-bit packing.
// - Only the source's default path is kept: its auto-tuned constants, no thin-occluder
//   compensation, the horizons unclamped. Bent normals, normals from depth, the debug
//   visualization and the alternatives the source disables are left out.
// - Every write is bounds-checked: the dispatches cover whole thread groups, and the prefilter
//   and denoise threads each write more than one texel.
// - The last denoise pass blends the term towards 1 by GTAOConstants::FinalIntensity.
// - XeGTAO_HilbertIndex gives the source's index but is written without its in-place swap,
//   see there.

#define XE_GTAO_PI      (3.1415926535897932384626433832795)
#define XE_GTAO_PI_HALF (1.5707963267948966192313216916398)

#define XE_GTAO_DEPTH_MIP_LEVELS 5

// The source's auto-tuned defaults.
#define XE_GTAO_DEFAULT_RADIUS_MULTIPLIER         (1.457)  // counters inherent screen space biases against the ground truth radius
#define XE_GTAO_DEFAULT_FALLOFF_RANGE             (0.615)  // distant samples contribute less
#define XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER (2.0)    // small crevices more important than big surfaces
#define XE_GTAO_DEFAULT_FINAL_VALUE_POWER         (2.2)    // modifies the final ambient occlusion value using power function
#define XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET (3.30)  // main trade-off between performance (memory bandwidth) and quality
#define XE_GTAO_DEFAULT_DENOISE_BLUR_BETA         (1.2)    // the source's value with denoising on

struct GTAOConstants
{
    int2   ViewportSize;
    float2 ViewportPixelSize;           // 1 / ViewportSize

    float2 DepthUnpackConsts;           // view depth = x / (y - device depth)

    float2 NDCToViewMul;                // screen position [0, 1] to view-space xy at depth 1
    float2 NDCToViewAdd;
    float2 NDCToViewMul_x_PixelSize;

    float  EffectRadius;                // world (viewspace) maximum size of the shadow
    float  FinalValuePower;
    float  DenoiseBlurBeta;
    float  DepthMIPSamplingOffset;
    float  FinalIntensity;              // 0: the output is 1 everywhere; 1: the full term
    int    NoiseIndex;                  // frame index % 64 if using TAA or 0 otherwise
};

// From https://www.shadertoy.com/view/3tB3z3 - except we're using R2 here
#define XE_HILBERT_LEVEL 6U     // the curve fills a 64x64 tile

// The source's index, without its in-place swap of posX and posY: the two never change here,
// and the swap and the flip it applies between levels are carried as two bits instead. The
// source's loop came out of the shader compiler or the driver with one swap losing posX (seen
// on an RTX 5070 Ti), which left a quarter of the 64x64 tile with an index of posY alone.
uint XeGTAO_HilbertIndex(uint posX, uint posY)
{
    uint index = 0U;
    uint swap  = 0U;
    uint flip  = 0U;
    for (uint level = XE_HILBERT_LEVEL; level > 0U; level--)
    {
        const uint bitX = ((posX >> (level - 1U)) & 1U) ^ flip;
        const uint bitY = ((posY >> (level - 1U)) & 1U) ^ flip;

        const uint exchanged = (bitX ^ bitY) & swap;
        const uint regionX   = bitX ^ exchanged;
        const uint regionY   = bitY ^ exchanged;

        index = (index << 2U) | ((3U * regionX) ^ regionY);

        const uint turn = regionY ^ 1U;
        swap ^= turn;
        flip ^= turn & regionX;
    }
    return index;
}

// Screen & temporal noise: a Hilbert curve driving R2 (see https://www.shadertoy.com/view/3tB3z3).
// Without TAA, temporalIndex is always 0.
float2 XeGTAO_SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
    uint index = XeGTAO_HilbertIndex(pixCoord.x, pixCoord.y);
    index += 288 * (temporalIndex % 64); // why 288? tried out a few and that's the best so far (with XE_HILBERT_LEVEL 6U) - but there's probably better :)
    // R2 sequence - see http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
    return frac(0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114));
}

// Inputs are screen XY and viewspace depth, output is viewspace position
float3 XeGTAO_ComputeViewspacePosition(const float2 screenPos, const float viewspaceDepth, const GTAOConstants consts)
{
    float3 ret;
    ret.xy = (consts.NDCToViewMul * screenPos.xy + consts.NDCToViewAdd) * viewspaceDepth;
    ret.z  = viewspaceDepth;
    return ret;
}

float XeGTAO_ScreenSpaceToViewSpaceDepth(const float screenDepth, const GTAOConstants consts)
{
    float depthLinearizeMul = consts.DepthUnpackConsts.x;
    float depthLinearizeAdd = consts.DepthUnpackConsts.y;
    return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
}

float4 XeGTAO_CalculateEdges(const float centerZ, const float leftZ, const float rightZ, const float topZ, const float bottomZ)
{
    float4 edgesLRTB = float4(leftZ, rightZ, topZ, bottomZ) - centerZ;

    float slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
    float slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
    float4 edgesLRTBSlopeAdjusted = edgesLRTB + float4(slopeLR, -slopeLR, slopeTB, -slopeTB);
    edgesLRTB = min(abs(edgesLRTB), abs(edgesLRTBSlopeAdjusted));
    return saturate(1.25 - edgesLRTB / (centerZ * 0.011));
}

// packing/unpacking for edges; 2 bits per edge mean 4 gradient values (0, 0.33, 0.66, 1) for smoother transitions!
float XeGTAO_PackEdges(float4 edgesLRTB)
{
    edgesLRTB = round(saturate(edgesLRTB) * 2.9);
    return dot(edgesLRTB, float4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

float4 XeGTAO_UnpackEdges(float _packedVal)
{
    uint packedVal = (uint)(_packedVal * 255.5);
    float4 edgesLRTB;
    edgesLRTB.x = float((packedVal >> 6) & 0x03) / 3.0;
    edgesLRTB.y = float((packedVal >> 4) & 0x03) / 3.0;
    edgesLRTB.z = float((packedVal >> 2) & 0x03) / 3.0;
    edgesLRTB.w = float((packedVal >> 0) & 0x03) / 3.0;

    return saturate(edgesLRTB);
}

// http://h14s.p5r.org/2012/09/0x5f3759df.html, [Drobot2014a] Low Level Optimizations for GCN, https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf slide 63
float XeGTAO_FastSqrt(float x)
{
    return asfloat(0x1fbd1df5 + (asint(x) >> 1));
}

// input [-1, 1] and output [0, PI], from https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
float XeGTAO_FastACos(float inX)
{
    const float PI = 3.141593;
    const float HALF_PI = 1.570796;
    float x = abs(inX);
    float res = -0.156583 * x + HALF_PI;
    res *= XeGTAO_FastSqrt(1.0 - x);
    return (inX >= 0) ? res : PI - res;
}

// The visibility of one pixel: the cosine-weighted share of its hemisphere that the depth
// buffer leaves open. viewspaceNormal is the pixel's; the caller keeps pixCoord inside the
// viewport.
void XeGTAO_MainPass(
    const uint2 pixCoord, float sliceCount, float stepsPerSlice, const float2 localNoise, float3 viewspaceNormal,
    const GTAOConstants consts, Texture2D<float> sourceViewspaceDepth, SamplerState depthSampler,
    RWTexture2D<float> outWorkingAOTerm, RWTexture2D<float> outWorkingEdges)
{
    float2 normalizedScreenPos = (pixCoord + 0.5.xx) * consts.ViewportPixelSize;

    float4 valuesUL = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * consts.ViewportPixelSize));
    float4 valuesBR = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * consts.ViewportPixelSize), int2(1, 1));

    // viewspace Z at the center
    float viewspaceZ = valuesUL.y;

    // viewspace Zs left top right bottom
    const float pixLZ = valuesUL.x;
    const float pixTZ = valuesUL.z;
    const float pixRZ = valuesBR.z;
    const float pixBZ = valuesBR.x;

    float4 edgesLRTB = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
    outWorkingEdges[pixCoord] = XeGTAO_PackEdges(edgesLRTB);

    // Move center pixel slightly towards camera to avoid imprecision artifacts due to depth buffer imprecision; offset depends on depth texture format used
    viewspaceZ *= 0.99999;     // this is good for FP32 depth buffer

    const float3 pixCenterPos = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ, consts);
    const float3 viewVec      = normalize(-pixCenterPos);

    const float effectRadius            = consts.EffectRadius * XE_GTAO_DEFAULT_RADIUS_MULTIPLIER;
    const float sampleDistributionPower = XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER;
    const float falloffRange            = XE_GTAO_DEFAULT_FALLOFF_RANGE * effectRadius;

    const float falloffFrom = effectRadius * (1.0 - XE_GTAO_DEFAULT_FALLOFF_RANGE);

    // fadeout precompute optimisation
    const float falloffMul = -1.0 / falloffRange;
    const float falloffAdd = falloffFrom / falloffRange + 1.0;

    float visibility = 0;

    // see "Algorithm 1" in https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
    {
        const float noiseSlice  = localNoise.x;
        const float noiseSample = localNoise.y;

        // quality settings / tweaks / hacks
        const float pixelTooCloseThreshold = 1.3;      // if the offset is under approx pixel size (pixelTooCloseThreshold), push it out to the minimum distance

        // approx viewspace pixel size at pixCoord; approximation of NDCToViewspace( normalizedScreenPos.xy + consts.ViewportPixelSize.xy, pixCenterPos.z ).xy - pixCenterPos.xy;
        const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * consts.NDCToViewMul_x_PixelSize;

        float screenspaceRadius = effectRadius / pixelDirRBViewspaceSizeAtCenterZ.x;

        // fade out for small screen radii
        visibility += saturate((10 - screenspaceRadius) / 100) * 0.5;

        // this is the min distance to start sampling from to avoid sampling from the center pixel (no useful data obtained from sampling center pixel)
        const float minS = pixelTooCloseThreshold / screenspaceRadius;

        for (float slice = 0; slice < sliceCount; slice++)
        {
            float sliceK = (slice + noiseSlice) / sliceCount;
            // lines 5, 6 from the paper
            float phi = sliceK * XE_GTAO_PI;
            float cosPhi = cos(phi);
            float sinPhi = sin(phi);
            float2 omega = float2(cosPhi, -sinPhi);

            // convert to screen units (pixels) for later use
            omega *= screenspaceRadius;

            // line 8 from the paper
            const float3 directionVec = float3(cosPhi, sinPhi, 0);

            // line 9 from the paper
            const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);

            // line 10 from the paper
            // axisVec is orthogonal to directionVec and viewVec, used to define projectedNormal
            const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

            // line 11 from the paper
            float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);

            // line 13 from the paper
            float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));

            // line 14 from the paper
            float projectedNormalVecLength = length(projectedNormalVec);
            float cosNorm = saturate(dot(projectedNormalVec, viewVec) / projectedNormalVecLength);

            // line 15 from the paper
            float n = signNorm * XeGTAO_FastACos(cosNorm);

            // this is a lower weight target; not using -1 as in the original paper because it is under horizon, so a 'weight' has different meaning based on the normal
            const float lowHorizonCos0 = cos(n + XE_GTAO_PI_HALF);
            const float lowHorizonCos1 = cos(n - XE_GTAO_PI_HALF);

            // lines 17, 18 from the paper, manually unrolled the 'side' loop
            float horizonCos0 = lowHorizonCos0; //-1;
            float horizonCos1 = lowHorizonCos1; //-1;

            for (float step = 0; step < stepsPerSlice; step++)
            {
                // R1 sequence (http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/)
                const float stepBaseNoise = (slice + step * stepsPerSlice) * 0.6180339887498948482;
                float stepNoise = frac(noiseSample + stepBaseNoise);

                // approx line 20 from the paper, with added noise
                float s = (step + stepNoise) / stepsPerSlice;

                // additional distribution modifier
                s = pow(s, sampleDistributionPower);

                // avoid sampling center pixel
                s += minS;

                // approx lines 21-22 from the paper, unrolled
                float2 sampleOffset = s * omega;

                float sampleOffsetLength = length(sampleOffset);

                // note: when sampling, using point_point_point or point_point_linear sampler works, but linear_linear_linear will cause unwanted interpolation between neighbouring depth values on the same MIP level!
                const float mipLevel = clamp(log2(sampleOffsetLength) - consts.DepthMIPSamplingOffset, 0, XE_GTAO_DEPTH_MIP_LEVELS);

                // Snap to pixel center (more correct direction math, avoids artifacts due to sampling pos not matching depth texel center - messes up slope - but adds other
                // artifacts due to them being pushed off the slice). Also use full precision for high res cases.
                sampleOffset = round(sampleOffset) * consts.ViewportPixelSize;

                float2 sampleScreenPos0 = normalizedScreenPos + sampleOffset;
                float  SZ0 = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos0, mipLevel).x;
                float3 samplePos0 = XeGTAO_ComputeViewspacePosition(sampleScreenPos0, SZ0, consts);

                float2 sampleScreenPos1 = normalizedScreenPos - sampleOffset;
                float  SZ1 = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos1, mipLevel).x;
                float3 samplePos1 = XeGTAO_ComputeViewspacePosition(sampleScreenPos1, SZ1, consts);

                float3 sampleDelta0 = samplePos0 - pixCenterPos;
                float3 sampleDelta1 = samplePos1 - pixCenterPos;
                float  sampleDist0  = length(sampleDelta0);
                float  sampleDist1  = length(sampleDelta1);

                // approx lines 23, 24 from the paper, unrolled
                float3 sampleHorizonVec0 = sampleDelta0 / sampleDist0;
                float3 sampleHorizonVec1 = sampleDelta1 / sampleDist1;

                // any sample out of radius should be discarded - also use fallof range for smooth transitions; this is a modified idea from "4.3 Implementation details, Bounding the sampling area"
                float weight0 = saturate(sampleDist0 * falloffMul + falloffAdd);
                float weight1 = saturate(sampleDist1 * falloffMul + falloffAdd);

                // sample horizon cos
                float shc0 = dot(sampleHorizonVec0, viewVec);
                float shc1 = dot(sampleHorizonVec1, viewVec);

                // discard unwanted samples
                shc0 = lerp(lowHorizonCos0, shc0, weight0); // this would be more correct but too expensive: cos(lerp( acos(lowHorizonCos0), acos(shc0), weight0 ));
                shc1 = lerp(lowHorizonCos1, shc1, weight1); // this would be more correct but too expensive: cos(lerp( acos(lowHorizonCos1), acos(shc1), weight1 ));

                horizonCos0 = max(horizonCos0, shc0);
                horizonCos1 = max(horizonCos1, shc1);
            }

            // I can't figure out the slight overdarkening on high slopes, so I'm adding this fudge - in the training set, 0.05 is close (PSNR 21.34) to disabled (PSNR 21.45)
            projectedNormalVecLength = lerp(projectedNormalVecLength, 1, 0.05);

            // line ~27, unrolled
            float h0 = -XeGTAO_FastACos(horizonCos1);
            float h1 =  XeGTAO_FastACos(horizonCos0);

            float iarc0 = (cosNorm + 2 * h0 * sin(n) - cos(2 * h0 - n)) / 4;
            float iarc1 = (cosNorm + 2 * h1 * sin(n) - cos(2 * h1 - n)) / 4;
            float localVisibility = projectedNormalVecLength * (iarc0 + iarc1);
            visibility += localVisibility;
        }
        visibility /= sliceCount;
        visibility = pow(visibility, consts.FinalValuePower);
        visibility = max(0.03, visibility); // disallow total occlusion (which wouldn't make any sense anyhow since pixel is visible)
    }

    outWorkingAOTerm[pixCoord] = visibility;
}

// weighted average depth filter
float XeGTAO_DepthMIPFilter(float depth0, float depth1, float depth2, float depth3, const GTAOConstants consts)
{
    float maxDepth = max(max(depth0, depth1), max(depth2, depth3));

    const float depthRangeScaleFactor = 0.75; // found empirically :)
    const float effectRadius = depthRangeScaleFactor * consts.EffectRadius * XE_GTAO_DEFAULT_RADIUS_MULTIPLIER;
    const float falloffRange = XE_GTAO_DEFAULT_FALLOFF_RANGE * effectRadius;
    const float falloffFrom  = effectRadius * (1.0 - XE_GTAO_DEFAULT_FALLOFF_RANGE);
    // fadeout precompute optimisation
    const float falloffMul = -1.0 / falloffRange;
    const float falloffAdd = falloffFrom / falloffRange + 1.0;

    float weight0 = saturate((maxDepth - depth0) * falloffMul + falloffAdd);
    float weight1 = saturate((maxDepth - depth1) * falloffMul + falloffAdd);
    float weight2 = saturate((maxDepth - depth2) * falloffMul + falloffAdd);
    float weight3 = saturate((maxDepth - depth3) * falloffMul + falloffAdd);

    float weightSum = weight0 + weight1 + weight2 + weight3;
    return (weight0 * depth0 + weight1 * depth1 + weight2 * depth2 + weight3 * depth3) / weightSum;
}

// This is also a good place to do non-linear depth conversion for cases where one wants the 'radius' (effectively the threshold between near-field and far-field GI),
// is required to be non-linear (i.e. very large outdoors environments).
float XeGTAO_ClampDepth(float depth)
{
    return clamp(depth, 0.0, 3.402823466e+38);
}

// The chain's mip `mip` is the viewport halved that many times, rounded down.
void XeGTAO_StoreDepth(RWTexture2D<float> outDepth, const uint2 coord, const uint mip, const GTAOConstants consts, const float depth)
{
    const uint2 size = max(uint2(consts.ViewportSize) >> mip, uint2(1, 1));
    if (all(coord < size))
    {
        outDepth[coord] = depth;
    }
}

// Each thread converts a 2x2 block of device depths and writes it to MIP 0, and the 8x8 threads
// of a group filter their 16x16 block down to MIPs 1-4. Dispatch needs to be called with
// (width + 16-1) / 16, (height + 16-1) / 16.
groupshared float g_scratchDepths[8][8];
void XeGTAO_PrefilterDepths16x16(
    uint2 dispatchThreadID, uint2 groupThreadID, const GTAOConstants consts, Texture2D<float> sourceNDCDepth, SamplerState depthSampler,
    RWTexture2D<float> outDepth0, RWTexture2D<float> outDepth1, RWTexture2D<float> outDepth2, RWTexture2D<float> outDepth3, RWTexture2D<float> outDepth4)
{
    // MIP 0
    const uint2 baseCoord = dispatchThreadID;
    const uint2 pixCoord = baseCoord * 2;
    float4 depths4 = sourceNDCDepth.GatherRed(depthSampler, float2(pixCoord * consts.ViewportPixelSize), int2(1, 1));
    float depth0 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.w, consts));
    float depth1 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.z, consts));
    float depth2 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.x, consts));
    float depth3 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.y, consts));
    XeGTAO_StoreDepth(outDepth0, pixCoord + uint2(0, 0), 0, consts, depth0);
    XeGTAO_StoreDepth(outDepth0, pixCoord + uint2(1, 0), 0, consts, depth1);
    XeGTAO_StoreDepth(outDepth0, pixCoord + uint2(0, 1), 0, consts, depth2);
    XeGTAO_StoreDepth(outDepth0, pixCoord + uint2(1, 1), 0, consts, depth3);

    // MIP 1
    float dm1 = XeGTAO_DepthMIPFilter(depth0, depth1, depth2, depth3, consts);
    XeGTAO_StoreDepth(outDepth1, baseCoord, 1, consts, dm1);
    g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm1;

    GroupMemoryBarrierWithGroupSync();

    // MIP 2
    [branch]
    if (all((groupThreadID.xy % 2.xx) == 0))
    {
        float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
        float inTR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 0];
        float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 1];
        float inBR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 1];

        float dm2 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR, consts);
        XeGTAO_StoreDepth(outDepth2, baseCoord / 2, 2, consts, dm2);
        g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm2;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 3
    [branch]
    if (all((groupThreadID.xy % 4.xx) == 0))
    {
        float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
        float inTR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 0];
        float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 2];
        float inBR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 2];

        float dm3 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR, consts);
        XeGTAO_StoreDepth(outDepth3, baseCoord / 4, 3, consts, dm3);
        g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm3;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 4
    [branch]
    if (all((groupThreadID.xy % 8.xx) == 0))
    {
        float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
        float inTR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 0];
        float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 4];
        float inBR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 4];

        float dm4 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR, consts);
        XeGTAO_StoreDepth(outDepth4, baseCoord / 8, 4, consts, dm4);
    }
}

void XeGTAO_AddSample(float ssaoValue, float edgeValue, inout float sum, inout float sumWeight)
{
    float weight = edgeValue;

    sum += (weight * ssaoValue);
    sumWeight += weight;
}

void XeGTAO_Output(int2 pixCoord, RWTexture2D<float> outputTexture, float outputValue, const GTAOConstants consts, const bool finalApply)
{
    if (any(pixCoord >= consts.ViewportSize))
    {
        return;
    }
    outputTexture[pixCoord] = finalApply ? lerp(1.0, saturate(outputValue), consts.FinalIntensity) : outputValue;
}

// Blurs the term over the 3x3 around each pixel, each neighbour weighted by the edges between
// it and the centre. A thread computes 2 horizontal pixels (performance optimization), so the
// dispatch covers half the width.
void XeGTAO_Denoise(
    const uint2 pixCoordBase, const GTAOConstants consts, Texture2D<float> sourceAOTerm, Texture2D<float> sourceEdges, SamplerState texSampler,
    RWTexture2D<float> outputTexture, const bool finalApply)
{
    const float blurAmount = finalApply ? consts.DenoiseBlurBeta : (consts.DenoiseBlurBeta / 5.0);
    const float diagWeight = 0.85 * 0.5;

    float  aoTerm[2];   // pixel pixCoordBase and pixel pixCoordBase + int2( 1, 0 )
    float4 edgesC_LRTB[2];
    float  weightTL[2];
    float  weightTR[2];
    float  weightBL[2];
    float  weightBR[2];

    // gather edge and visibility quads, used later
    const float2 gatherCenter = float2(pixCoordBase.x, pixCoordBase.y) * consts.ViewportPixelSize;
    float4 edgesQ0 = sourceEdges.GatherRed(texSampler, gatherCenter, int2(0, 0));
    float4 edgesQ1 = sourceEdges.GatherRed(texSampler, gatherCenter, int2(2, 0));
    float4 edgesQ2 = sourceEdges.GatherRed(texSampler, gatherCenter, int2(1, 2));

    float4 visQ0 = sourceAOTerm.GatherRed(texSampler, gatherCenter, int2(0, 0));
    float4 visQ1 = sourceAOTerm.GatherRed(texSampler, gatherCenter, int2(2, 0));
    float4 visQ2 = sourceAOTerm.GatherRed(texSampler, gatherCenter, int2(0, 2));
    float4 visQ3 = sourceAOTerm.GatherRed(texSampler, gatherCenter, int2(2, 2));

    for (int side = 0; side < 2; side++)
    {
        const int2 pixCoord = int2(pixCoordBase.x + side, pixCoordBase.y);

        float4 edgesL_LRTB = XeGTAO_UnpackEdges((side == 0) ? (edgesQ0.x) : (edgesQ0.y));
        float4 edgesT_LRTB = XeGTAO_UnpackEdges((side == 0) ? (edgesQ0.z) : (edgesQ1.w));
        float4 edgesR_LRTB = XeGTAO_UnpackEdges((side == 0) ? (edgesQ1.x) : (edgesQ1.y));
        float4 edgesB_LRTB = XeGTAO_UnpackEdges((side == 0) ? (edgesQ2.w) : (edgesQ2.z));

        edgesC_LRTB[side] = XeGTAO_UnpackEdges((side == 0) ? (edgesQ0.y) : (edgesQ1.x));

        // Edges aren't perfectly symmetrical: edge detection algorithm does not guarantee that a left edge on the right pixel will match the right edge on the left pixel (although
        // they will match in majority of cases). This line further enforces the symmetricity, creating a slightly sharper blur. Works real nice with TAA.
        edgesC_LRTB[side] *= float4(edgesL_LRTB.y, edgesR_LRTB.x, edgesT_LRTB.w, edgesB_LRTB.z);

        // this allows some small amount of AO leaking from neighbours if there are 3 or 4 edges; this reduces both spatial and temporal aliasing
        const float leak_threshold = 2.5;
        const float leak_strength = 0.5;
        float edginess = (saturate(4.0 - leak_threshold - dot(edgesC_LRTB[side], 1.xxxx)) / (4 - leak_threshold)) * leak_strength;
        edgesC_LRTB[side] = saturate(edgesC_LRTB[side] + edginess);

        // for diagonals; used by first and second pass
        weightTL[side] = diagWeight * (edgesC_LRTB[side].x * edgesL_LRTB.z + edgesC_LRTB[side].z * edgesT_LRTB.x);
        weightTR[side] = diagWeight * (edgesC_LRTB[side].z * edgesT_LRTB.y + edgesC_LRTB[side].y * edgesR_LRTB.z);
        weightBL[side] = diagWeight * (edgesC_LRTB[side].w * edgesB_LRTB.x + edgesC_LRTB[side].x * edgesL_LRTB.w);
        weightBR[side] = diagWeight * (edgesC_LRTB[side].y * edgesR_LRTB.w + edgesC_LRTB[side].w * edgesB_LRTB.y);

        // first pass
        float ssaoValue   = (side == 0) ? (visQ0[1]) : (visQ1[0]);
        float ssaoValueL  = (side == 0) ? (visQ0[0]) : (visQ0[1]);
        float ssaoValueT  = (side == 0) ? (visQ0[2]) : (visQ1[3]);
        float ssaoValueR  = (side == 0) ? (visQ1[0]) : (visQ1[1]);
        float ssaoValueB  = (side == 0) ? (visQ2[2]) : (visQ3[3]);
        float ssaoValueTL = (side == 0) ? (visQ0[3]) : (visQ0[2]);
        float ssaoValueBR = (side == 0) ? (visQ3[3]) : (visQ3[2]);
        float ssaoValueTR = (side == 0) ? (visQ1[3]) : (visQ1[2]);
        float ssaoValueBL = (side == 0) ? (visQ2[3]) : (visQ2[2]);

        float sumWeight = blurAmount;
        float sum = ssaoValue * sumWeight;

        XeGTAO_AddSample(ssaoValueL, edgesC_LRTB[side].x, sum, sumWeight);
        XeGTAO_AddSample(ssaoValueR, edgesC_LRTB[side].y, sum, sumWeight);
        XeGTAO_AddSample(ssaoValueT, edgesC_LRTB[side].z, sum, sumWeight);
        XeGTAO_AddSample(ssaoValueB, edgesC_LRTB[side].w, sum, sumWeight);

        XeGTAO_AddSample(ssaoValueTL, weightTL[side], sum, sumWeight);
        XeGTAO_AddSample(ssaoValueTR, weightTR[side], sum, sumWeight);
        XeGTAO_AddSample(ssaoValueBL, weightBL[side], sum, sumWeight);
        XeGTAO_AddSample(ssaoValueBR, weightBR[side], sum, sumWeight);

        aoTerm[side] = sum / sumWeight;

        XeGTAO_Output(pixCoord, outputTexture, aoTerm[side], consts, finalApply);
    }
}

#endif // SPARK_LIB_XEGTAO_HLSLI
