// Bicubic PCF reconstruction, built out of hardware bilinear comparison fetches.
//
// A comparison sampler compares first and interpolates after, so one SampleCmp is already a
// 2x2 box PCF. Placing that tap OFF the texel centre re-weights the four texels it covers:
// to get w0*A + w1*B from one fetch, sit at t = w1/(w0+w1) between them and scale the result
// by (w0+w1). A separable kernel therefore costs one tap per 2x2 block rather than four, and
// an NxN weighted kernel collapses to ((N+1)/2)^2 taps.
//
// The weights below are the ones Cry shipped and Atom inherited; the 4-tap case works out to
// a width-4 tent in each axis, and the final squaring bends its linear penumbra ramp into a
// quadratic one — which is the shape a cubic B-spline has, and the whole of "bicubic" here.
//
// Nothing in this file is bound. The atlas and its sampler arrive as arguments so the same
// kernels serve any shadow resource, and the caller owns every binding decision.
//
// References:
//   https://web.archive.org/web/20201222005801/https:/vec3.ca/bicubic-filtering-in-fewer-taps/
//   GPU Gems 2, ch. 20 — Fast Third-Order Texture Filtering
#ifndef SPARK_LIB_SHADOW_BICUBIC_PCF_HLSLI
#define SPARK_LIB_SHADOW_BICUBIC_PCF_HLSLI

struct ShadowFilterInput
{
    float2 uv;            // atlas UV of the shaded point
    float  z;             // comparison depth, bias already applied

    //! (minU, minV, maxU, maxV) at the tile's texel CENTRES, not its edges. Every tap clamps
    //! here, so no kernel width can reach a neighbouring tile — whose depth belongs to an
    //! unrelated light and would compare to an arbitrary result.
    float4 uvClamp;

    //! The ATLAS resolution, never the tile's. Tiles are powers of two on integer texel
    //! bounds, so the two grids coincide and the floor below aligns for both; sizing by the
    //! tile instead puts every tap half a texel off, which reads as blur rather than as a bug.
    float  atlasSize;
    float  invAtlasSize;
};

float BicubicTap(Texture2D atlas, SamplerComparisonState smp,
                 ShadowFilterInput input, float2 base, float2 offsetTexels)
{
    float2 uv = clamp(base + offsetTexels * input.invAtlasSize, input.uvClamp.xy, input.uvClamp.zw);
    return atlas.SampleCmpLevelZero(smp, uv, input.z);
}

//! 3x3 kernel in 4 taps. Half-width 1.5 texels.
float BicubicPcf4Tap(Texture2D atlas, SamplerComparisonState smp, ShadowFilterInput input)
{
    float2 scaled = input.uv * input.atlasSize + 0.5;
    float2 grid   = floor(scaled);
    float2 f      = scaled - grid;

    // Lands on a texel centre, and the sampled point sits exactly f texels from it.
    float2 base = (grid - 0.5) * input.invAtlasSize;

    float2 w0 = 3.0 - 2.0 * f;
    float2 w1 = 1.0 + 2.0 * f;

    float2 o0 = (2.0 - f) / w0 - 1.0;
    float2 o1 = f / w1 + 1.0;

    float sum = w0.x * w0.y * BicubicTap(atlas, smp, input, base, float2(o0.x, o0.y))
              + w1.x * w0.y * BicubicTap(atlas, smp, input, base, float2(o1.x, o0.y))
              + w0.x * w1.y * BicubicTap(atlas, smp, input, base, float2(o0.x, o1.y))
              + w1.x * w1.y * BicubicTap(atlas, smp, input, base, float2(o1.x, o1.y));

    sum *= 1.0 / 16.0;
    return sum * sum;
}

//! 5x5 kernel in 9 taps. Half-width 2.5 texels.
float BicubicPcf9Tap(Texture2D atlas, SamplerComparisonState smp, ShadowFilterInput input)
{
    float2 scaled = input.uv * input.atlasSize + 0.5;
    float2 grid   = floor(scaled);
    float2 f      = scaled - grid;

    float2 base = (grid - 0.5) * input.invAtlasSize;

    float2 w0 = 4.0 - 3.0 * f;
    float2 w1 = 7.0;
    float2 w2 = 1.0 + 3.0 * f;

    float2 o0 = (3.0 - 2.0 * f) / w0 - 2.0;
    float2 o1 = (3.0 + f) / w1;
    float2 o2 = f / w2 + 2.0;

    const float2 offsets[9] = {
        float2(o0.x, o0.y), float2(o1.x, o0.y), float2(o2.x, o0.y),
        float2(o0.x, o1.y), float2(o1.x, o1.y), float2(o2.x, o1.y),
        float2(o0.x, o2.y), float2(o1.x, o2.y), float2(o2.x, o2.y),
    };
    const float weights[9] = {
        w0.x * w0.y, w1.x * w0.y, w2.x * w0.y,
        w0.x * w1.y, w1.x * w1.y, w2.x * w1.y,
        w0.x * w2.y, w1.x * w2.y, w2.x * w2.y,
    };

    float sum = 0.0;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        sum += weights[i] * BicubicTap(atlas, smp, input, base, offsets[i]);
    }

    sum *= 1.0 / 144.0;
    return sum * sum;
}

//! 7x7 kernel in 16 taps. Half-width 3.5 texels. Every weight here is negative; the products
//! that actually scale a tap are pairs of them, so the kernel is positive throughout.
float BicubicPcf16Tap(Texture2D atlas, SamplerComparisonState smp, ShadowFilterInput input)
{
    float2 scaled = input.uv * input.atlasSize + 0.5;
    float2 grid   = floor(scaled);
    float2 f      = scaled - grid;

    float2 base = (grid - 0.5) * input.invAtlasSize;

    float2 w0 = 5.0 * f - 6.0;
    float2 w1 = 11.0 * f - 28.0;
    float2 w2 = -(11.0 * f + 17.0);
    float2 w3 = -(5.0 * f + 1.0);

    float2 o0 = (4.0 * f - 5.0) / w0 - 3.0;
    float2 o1 = (4.0 * f - 16.0) / w1 - 1.0;
    float2 o2 = -((7.0 * f + 5.0) / w2) + 1.0;
    float2 o3 = -(f / w3) + 3.0;

    const float2 offsets[16] = {
        float2(o0.x, o0.y), float2(o1.x, o0.y), float2(o2.x, o0.y), float2(o3.x, o0.y),
        float2(o0.x, o1.y), float2(o1.x, o1.y), float2(o2.x, o1.y), float2(o3.x, o1.y),
        float2(o0.x, o2.y), float2(o1.x, o2.y), float2(o2.x, o2.y), float2(o3.x, o2.y),
        float2(o0.x, o3.y), float2(o1.x, o3.y), float2(o2.x, o3.y), float2(o3.x, o3.y),
    };
    const float weights[16] = {
        w0.x * w0.y, w1.x * w0.y, w2.x * w0.y, w3.x * w0.y,
        w0.x * w1.y, w1.x * w1.y, w2.x * w1.y, w3.x * w1.y,
        w0.x * w2.y, w1.x * w2.y, w2.x * w2.y, w3.x * w2.y,
        w0.x * w3.y, w1.x * w3.y, w2.x * w3.y, w3.x * w3.y,
    };

    float sum = 0.0;
    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        sum += weights[i] * BicubicTap(atlas, smp, input, base, offsets[i]);
    }

    sum *= 1.0 / 2704.0;
    return sum * sum;
}

//! Picks the kernel whose half-width the caller asked for. The offsets are authored in whole
//! texels, so the width is NOT continuous — it steps 1.5 / 2.5 / 3.5 and nothing between.
float BicubicPcf(Texture2D atlas, SamplerComparisonState smp,
                 ShadowFilterInput input, float halfWidthTexels)
{
    if (halfWidthTexels < 2.0)
    {
        return BicubicPcf4Tap(atlas, smp, input);
    }
    if (halfWidthTexels < 3.0)
    {
        return BicubicPcf9Tap(atlas, smp, input);
    }
    return BicubicPcf16Tap(atlas, smp, input);
}

#endif // SPARK_LIB_SHADOW_BICUBIC_PCF_HLSLI
