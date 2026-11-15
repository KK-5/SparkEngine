/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

// HLSL adaptation for SparkEngine of Blender v4.5.0 Cycles:
// https://github.com/blender/blender/tree/v4.5.0/intern/cycles
//   kernel/sample/sobol_burley.h, kernel/sample/util.h, kernel/tables.h, util/hash.h
// Based on Brent Burley's "Practical Hash-based Owen Scrambling" (JCGT, 2020).
// Changes: standalone HLSL types/intrinsics, full 32-bit index mask, pixel seed helper.
// See LICENSE-Cycles.txt for the Apache-2.0 license.
#ifndef SPARK_LIB_SOBOL_BURLEY_HLSLI
#define SPARK_LIB_SOBOL_BURLEY_HLSLI

// Direction vectors for the first four Sobol dimensions, stored with reversed bits
// for hash-based Owen scrambling. Higher dimensions use decorrelated sequence groups.
static const uint SobolBurleyDirections[4][32] =
{
    {
        0x00000001u, 0x00000002u, 0x00000004u, 0x00000008u,
        0x00000010u, 0x00000020u, 0x00000040u, 0x00000080u,
        0x00000100u, 0x00000200u, 0x00000400u, 0x00000800u,
        0x00001000u, 0x00002000u, 0x00004000u, 0x00008000u,
        0x00010000u, 0x00020000u, 0x00040000u, 0x00080000u,
        0x00100000u, 0x00200000u, 0x00400000u, 0x00800000u,
        0x01000000u, 0x02000000u, 0x04000000u, 0x08000000u,
        0x10000000u, 0x20000000u, 0x40000000u, 0x80000000u
    },
    {
        0x00000001u, 0x00000003u, 0x00000005u, 0x0000000fu,
        0x00000011u, 0x00000033u, 0x00000055u, 0x000000ffu,
        0x00000101u, 0x00000303u, 0x00000505u, 0x00000f0fu,
        0x00001111u, 0x00003333u, 0x00005555u, 0x0000ffffu,
        0x00010001u, 0x00030003u, 0x00050005u, 0x000f000fu,
        0x00110011u, 0x00330033u, 0x00550055u, 0x00ff00ffu,
        0x01010101u, 0x03030303u, 0x05050505u, 0x0f0f0f0fu,
        0x11111111u, 0x33333333u, 0x55555555u, 0xffffffffu
    },
    {
        0x00000001u, 0x00000003u, 0x00000006u, 0x00000009u,
        0x00000017u, 0x0000003au, 0x00000071u, 0x000000a3u,
        0x00000116u, 0x00000339u, 0x00000677u, 0x000009aau,
        0x00001601u, 0x00003903u, 0x00007706u, 0x0000aa09u,
        0x00010117u, 0x0003033au, 0x00060671u, 0x000909a3u,
        0x00171616u, 0x003a3939u, 0x00717777u, 0x00a3aaaau,
        0x01170001u, 0x033a0003u, 0x06710006u, 0x09a30009u,
        0x16160017u, 0x3939003au, 0x77770071u, 0xaaaa00a3u
    },
    {
        0x00000001u, 0x00000003u, 0x00000004u, 0x0000000au,
        0x0000001fu, 0x0000002eu, 0x00000045u, 0x000000c9u,
        0x0000011bu, 0x000002a4u, 0x0000079au, 0x00000b67u,
        0x0000101eu, 0x0000302du, 0x00004041u, 0x0000a0c3u,
        0x0001f104u, 0x0002e28au, 0x000457dfu, 0x000c9baeu,
        0x0011a105u, 0x002a7289u, 0x0079e7dbu, 0x00b6dba4u,
        0x0100011au, 0x030002a7u, 0x0400079eu, 0x0a000b6du,
        0x1f001001u, 0x2e003003u, 0x45004004u, 0xc900a00au
    }
};

// Cycles' Hash Prospector mixer: https://github.com/skeeto/hash-prospector
uint SobolBurleyHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x21f0aaadu;
    value ^= value >> 15u;
    value *= 0xd35a2d97u;
    value ^= value >> 15u;
    return value ^ 0xe6fe3bebu;
}

// Base-2 Owen scrambling on reversed-bit integers. All arithmetic wraps as uint32.
uint SobolBurleyScrambleReversedBits(uint value, uint seed)
{
    value ^= value * 0x3d20adeau;
    value += seed;
    value *= (seed >> 16u) | 1u;
    value ^= value * 0x05526c56u;
    value ^= value * 0x53a22864u;
    return value;
}

uint SobolBurleyCountLeadingZeros(uint value)
{
    if (value == 0u)
    {
        return 32u;
    }
    return 31u - uint(firstbithigh(value));
}

// Cycles' exclusive conversion. Using 2^32 as the divisor can round uint_max to 1.0.
float SobolBurleyUintToFloatExclusive(uint value)
{
    return float(value) * (1.0f / 4294967808.0f);
}

// Internal component evaluation: dimension is in [0, 3], and the index has already
// been shuffled in reversed-bit order. scrambleSeed must be well mixed.
float SobolBurleyEvaluateDimension(uint reversedIndex, uint dimension, uint scrambleSeed)
{
    uint result = 0u;
    if (dimension == 0u)
    {
        result = reversebits(reversedIndex);
    }
    else
    {
        uint bitIndex = 0u;
        while (reversedIndex != 0u)
        {
            const uint leadingZeros = SobolBurleyCountLeadingZeros(reversedIndex);
            result ^= SobolBurleyDirections[dimension][bitIndex + leadingZeros];
            bitIndex += leadingZeros + 1u;
            // Keep separate shifts: leadingZeros + 1 may be 32, which must not be
            // reduced modulo 32 by a shader backend's shift instruction.
            reversedIndex <<= leadingZeros;
            reversedIndex <<= 1u;
        }
    }

    result = reversebits(SobolBurleyScrambleReversedBits(result, scrambleSeed));
    return SobolBurleyUintToFloatExclusive(result);
}

// SparkEngine helper using the upstream mixer. Keep this seed fixed for a pixel
// across samples; sampleIndex supplies progression, not a new seed each frame.
uint MakeSobolBurleyPixelSeed(uint2 pixel, uint globalSeed)
{
    return SobolBurleyHash(pixel.x ^ SobolBurleyHash(pixel.y ^ SobolBurleyHash(globalSeed)));
}

// Stateless samples in [0, 1). The Cycles shuffled_index_mask is fixed to 0xffffffffu,
// so all 32 index bits are retained and no total sample count is required.
// Different 1D dimensions and different multidimensional sets are decorrelated by
// shuffling. Each 2D/3D/4D set retains its own stratification, rather than forming a
// single high-dimensional Sobol net. Assign sets by sampling purpose and bounce.
// The 1D/2D/3D/4D interfaces intentionally use distinct scramble/shuffle seeds even
// for identical arguments. Use the matching vector interface for joint sampling.
float SobolBurley1D(uint sampleIndex, uint dimension, uint seed)
{
    seed ^= SobolBurleyHash(dimension);
    const uint reversedIndex = SobolBurleyScrambleReversedBits(reversebits(sampleIndex), seed ^ 0xbff95bfeu);
    return SobolBurleyEvaluateDimension(reversedIndex, 0u, seed ^ 0x635c77bdu);
}

float2 SobolBurley2D(uint sampleIndex, uint dimensionSet, uint seed)
{
    seed ^= SobolBurleyHash(dimensionSet);
    const uint reversedIndex = SobolBurleyScrambleReversedBits(reversebits(sampleIndex), seed ^ 0xf8ade99au);
    return float2(
        SobolBurleyEvaluateDimension(reversedIndex, 0u, seed ^ 0xe0aaaf76u),
        SobolBurleyEvaluateDimension(reversedIndex, 1u, seed ^ 0x94964d4eu));
}

float3 SobolBurley3D(uint sampleIndex, uint dimensionSet, uint seed)
{
    seed ^= SobolBurleyHash(dimensionSet);
    const uint reversedIndex = SobolBurleyScrambleReversedBits(reversebits(sampleIndex), seed ^ 0xcaa726acu);
    return float3(
        SobolBurleyEvaluateDimension(reversedIndex, 0u, seed ^ 0x9e78e391u),
        SobolBurleyEvaluateDimension(reversedIndex, 1u, seed ^ 0x67c33241u),
        SobolBurleyEvaluateDimension(reversedIndex, 2u, seed ^ 0x78c395c5u));
}

float4 SobolBurley4D(uint sampleIndex, uint dimensionSet, uint seed)
{
    seed ^= SobolBurleyHash(dimensionSet);
    const uint reversedIndex = SobolBurleyScrambleReversedBits(reversebits(sampleIndex), seed ^ 0xc2c1a055u);
    return float4(
        SobolBurleyEvaluateDimension(reversedIndex, 0u, seed ^ 0x39468210u),
        SobolBurleyEvaluateDimension(reversedIndex, 1u, seed ^ 0xe9d8a845u),
        SobolBurleyEvaluateDimension(reversedIndex, 2u, seed ^ 0x5f32b482u),
        SobolBurleyEvaluateDimension(reversedIndex, 3u, seed ^ 0x1524cc56u));
}

#endif // SPARK_LIB_SOBOL_BURLEY_HLSLI
