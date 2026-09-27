#ifndef SPARK_LIB_AGX_HLSLI
#define SPARK_LIB_AGX_HLSLI

// Troy Sobotka's AgX display transform, scene-linear Rec.709 in, display-linear Rec.709 [0, 1]
// out. Ported from Filament's AgxToneMapper (filament/src/ToneMapper.cpp, Copyright (C) 2021
// The Android Open Source Project, Apache License 2.0), which takes its matrices from Blender's
// AgX (https://github.com/EaryChow/AgX_LUT_Gen) and its sigmoid fit from
// https://iolite-engine.com/blog_posts/minimal_agx_implementation. The Rec.709 <-> Rec.2020
// matrices are ITU-R BT.2407's, as three.js's AgXToneMapping uses them.
//
// Changes from Filament: it takes Rec.709 in and out rather than Rec.2020, and the look is
// Blender's, not Filament's CDL after the sigmoid (AgXLook below).
//
// The source matrices are column-major (each row of numbers below is one of their columns), so
// they multiply vector-first: mul(v, M) here is the source's M * v.

static const float3x3 kAgXRec709ToRec2020 = float3x3(
    0.6274, 0.0691, 0.0164,
    0.3293, 0.9195, 0.0880,
    0.0433, 0.0113, 0.8956);

static const float3x3 kAgXRec2020ToRec709 = float3x3(
     1.6605, -0.1246, -0.0182,
    -0.5876,  1.1329, -0.1006,
    -0.0728, -0.0083,  1.1187);

// Pulls each primary a little towards white and rotates it slightly: every color gets some of
// every channel, so as the brightest channel reaches the curve's shoulder the others catch up,
// and bright color goes to white instead of clipping or skewing in hue.
static const float3x3 kAgXInset = float3x3(
    0.856627153315983,  0.137318972929847, 0.11189821299995,
    0.0951212405381588, 0.761241990602591, 0.0767994186031903,
    0.0482516061458583, 0.101439036467562, 0.811302368396859);

// Filament's inverse(AgXOutsetMatrixInv), evaluated.
static const float3x3 kAgXOutset = float3x3(
     1.127100581814437,   -0.1413297634984383,  -0.1413297634984383,
    -0.1106066430966032,   1.157823702216272,   -0.1106066430966029,
    -0.01649393871783457, -0.01649393871783426,  1.25193640659504);

// The log2 range the sigmoid spans, in stops around 0.18 middle grey.
static const float kAgXMinEv = -12.47393;
static const float kAgXMaxEv = 4.026069;

// 0.18 on that log2 encoding: the pivot of the looks' contrast.
static const float kAgXMiddleGrey = (-2.4739311883324122 - kAgXMinEv) / (kAgXMaxEv - kAgXMinEv);

// Blender's Greyscale look: luminance of the inset's linear values.
static const float3 kAgXGreyscaleWeights = float3(0.2589235355689848, 0.6104985346066525, 0.13057792982436284);

// The AgX base contrast sigmoid over [0, 1], a 7th order polynomial fit.
float3 AgXContrast(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    const float3 x6 = x4 * x2;
    return - 17.86    * x6 * x
           + 78.01    * x6
           - 126.7    * x4 * x
           + 92.06    * x4
           - 28.72    * x2 * x
           + 4.361    * x2
           - 0.1718   * x
           + 0.002857;
}

// Blender's contrast looks (config.ocio, "AgX - ... Contrast"), as OCIO's log-style
// GradingPrimary applies them in AgX Log: contrast around middle grey, then saturation around
// luma. Both act on the log2 encoding before the sigmoid, so they move exposure in stops and
// the curve's toe and shoulder still hold black and white. In stops around middle grey neither
// depends on the encoding's range, so Blender's values, set on its 25-stop AgX Log, carry over
// to this 16.5-stop one exactly.
float3 AgXLook(float3 x, float contrast, float saturation)
{
    x = kAgXMiddleGrey + (x - kAgXMiddleGrey) * contrast;
    const float luma = dot(x, float3(0.2126, 0.7152, 0.0722));
    return luma + saturation * (x - luma);
}

float3 AgX(float3 color, float contrast, float saturation, bool greyscale)
{
    color = mul(max(color, 0.0), kAgXRec709ToRec2020);
    color = mul(color, kAgXInset);
    if (greyscale)
    {
        color = dot(color, kAgXGreyscaleWeights);
    }
    color = log2(max(color, 1e-10));
    color = (color - kAgXMinEv) / (kAgXMaxEv - kAgXMinEv);
    color = saturate(AgXLook(color, contrast, saturation));
    color = AgXContrast(color);
    color = mul(color, kAgXOutset);
    // The sigmoid's output is display-encoded (a 2.2 power); decode it, so what leaves is linear
    // and the caller's OETF encodes it once.
    color = pow(max(color, 0.0), 2.2);
    color = mul(color, kAgXRec2020ToRec709);
    return saturate(color);
}

#endif // SPARK_LIB_AGX_HLSLI
