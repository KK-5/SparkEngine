#ifndef SPARK_LIB_SAMPLE_HLSLI
#define SPARK_LIB_SAMPLE_HLSLI

// Low-discrepancy (quasi-random) sequence utilities for quasi-Monte Carlo sampling —
// importance sampling for IBL bakes (irradiance, prefilter) and anywhere else that needs
// evenly distributed sample points. Deterministic: the same index yields the same point,
// so no RNG state is threaded through the shader.
//
// Dimensionality note: one independent uniform variate is needed per random dimension of a
// sample (a hemisphere direction is 2D: θ, φ). Each extra dimension must use a DIFFERENT
// prime base for its radical inverse (Halton), or the coordinates collapse onto a diagonal.
// The bit-reversal RadicalInverse_VdC below is base-2 ONLY; higher dimensions use the
// general RadicalInverse(i, base) with 3, 5, 7, ... Hammersley replaces the first dimension
// with i/N (lower discrepancy, but N must be known up front — true for a fixed-count bake).

// Van der Corput radical inverse, base 2, via 32-bit reversal. Reverses the bits of i and
// places them after the binary point → an evenly space-filling 1D sequence in [0,1).
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 2^32
}

// General radical inverse in an arbitrary base. Use for the 3rd dimension onward (base 3),
// 4th (base 5), ... — one distinct prime per dimension. Base 2 has the faster bit-reversal
// form above; prefer that for the base-2 dimension.
float RadicalInverse(uint i, uint base)
{
    float invBase = 1.0 / float(base);
    float f       = invBase;
    float r       = 0.0;
    while (i > 0u)
    {
        r += float(i % base) * f;
        i /= base;
        f *= invBase;
    }
    return r;
}

// Hammersley 2D point set: (i/N, φ₂(i)). N must be the total sample count known in advance.
// The i/N first coordinate gives lower discrepancy than a 2D Halton for a fixed-size set.
float2 Hammersley(uint i, uint n)
{
    return float2(float(i) / float(n), RadicalInverse_VdC(i));
}

#endif // SPARK_LIB_SAMPLE_HLSLI
