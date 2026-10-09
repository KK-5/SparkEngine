#ifndef SPARK_LIB_LTC_HLSLI
#define SPARK_LIB_LTC_HLSLI

// Linearly Transformed Cosines: the integral of a GGX lobe over a polygonal light, done by
// transforming the polygon so the lobe becomes a clamped cosine, whose polygon integral has
// a closed form. Derivation and table layout: Document/TODO_AreaLightPlan.md, section 1.
//
// Pure library: the tables are sampled by the caller, which hands in the inverse matrix.
//
// Ported from webgl/shaders/ltc/ltc_quad.fs of https://github.com/selfshadow/ltc_code, the
// reference implementation of
//
//   Real-Time Polygonal-Light Shading with Linearly Transformed Cosines.
//   Eric Heitz, Jonathan Dupuy, Stephen Hill and David Neubelt.
//   ACM Transactions on Graphics (Proceedings of ACM SIGGRAPH 2016) 35(4), 2016.
//   Project page: https://eheitzresearch.wordpress.com/415-2/
//
//   Copyright (c) 2017, Eric Heitz, Jonathan Dupuy, Stephen Hill and David Neubelt.
//   All rights reserved.
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are met:
//
//   * If you use (or adapt) the source code in your own work, please include a
//     reference to the paper (above).
//
//   * Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
//   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
//   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Changes from the source: matrices are built by row for mul(M, v); the clipless horizon
// approximation and the two-sided option are left out; the tangent frame survives V == N.

static const float kLTCLutSize = 64.0;

//! Texel centres of the 64x64 tables. NoV must already be in [0, 1].
float2 LTCTableUV(float perceptualRoughness, float NoV)
{
    float2 uv = float2(perceptualRoughness, sqrt(1.0 - NoV));
    return uv * ((kLTCLutSize - 1.0) / kLTCLutSize) + 0.5 / kLTCLutSize;
}

//! The source builds this by column, mat3(vec3(x, 0, y), vec3(0, 1, 0), vec3(z, 0, w)); these
//! are the same matrix's rows, so y and z trade places on the page.
float3x3 LTCInverseMatrix(float4 t1)
{
    return float3x3(
        t1.x, 0.0, t1.z,
        0.0,  1.0, 0.0,
        t1.y, 0.0, t1.w);
}

//! One edge's term of Lambert's polygon formula, as a vector; its z is the edge's share of
//! the form factor. theta / sin(theta) comes from a rational fit rather than acos, which
//! bands at small angles, and the 1 / (2 pi) is folded into the fit.
float3 LTCIntegrateEdgeVec(float3 v1, float3 v2)
{
    float x = dot(v1, v2);
    float y = abs(x);

    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;

    float thetaSinTheta = (x > 0.0) ? v : 0.5 * rsqrt(max(1.0 - x * x, 1e-7)) - v;

    return cross(v1, v2) * thetaSinTheta;
}

float LTCIntegrateEdge(float3 v1, float3 v2)
{
    return LTCIntegrateEdgeVec(v1, v2).z;
}

//! Lambert's formula holds only above the horizon, so the quad is cut at z = 0 first. Leaves
//! n vertices (0, 3, 4 or 5) and closes the loop by repeating the first after the last.
void LTCClipQuadToHorizon(inout float3 L[5], out int n)
{
    int config = 0;
    if (L[0].z > 0.0) { config += 1; }
    if (L[1].z > 0.0) { config += 2; }
    if (L[2].z > 0.0) { config += 4; }
    if (L[3].z > 0.0) { config += 8; }

    n = 0;

    if (config == 1) // V1 clip V2 V3 V4
    {
        n = 3;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 2) // V2 clip V1 V3 V4
    {
        n = 3;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 3) // V1 V2 clip V3 V4
    {
        n = 4;
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
        L[3] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 4) // V3 clip V1 V2 V4
    {
        n = 3;
        L[0] = -L[3].z * L[2] + L[2].z * L[3];
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
    }
    else if (config == 6) // V2 V3 clip V1 V4
    {
        n = 4;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 7) // V1 V2 V3 clip V4
    {
        n = 5;
        L[4] = -L[3].z * L[0] + L[0].z * L[3];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 8) // V4 clip V1 V2 V3
    {
        n = 3;
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
        L[1] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] =  L[3];
    }
    else if (config == 9) // V1 V4 clip V2 V3
    {
        n = 4;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[2].z * L[3] + L[3].z * L[2];
    }
    else if (config == 11) // V1 V2 V4 clip V3
    {
        n = 5;
        L[4] = L[3];
        L[3] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 12) // V3 V4 clip V1 V2
    {
        n = 4;
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
    }
    else if (config == 13) // V1 V3 V4 clip V2
    {
        n = 5;
        L[4] = L[3];
        L[3] = L[2];
        L[2] = -L[1].z * L[2] + L[2].z * L[1];
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
    }
    else if (config == 14) // V2 V3 V4 clip V1
    {
        n = 5;
        L[4] = -L[0].z * L[3] + L[3].z * L[0];
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
    }
    else if (config == 15) // V1 V2 V3 V4
    {
        n = 4;
    }
    // 0 is wholly below; 5 and 10 (opposite corners only) cannot happen to a convex quad.

    if (n == 3)
    {
        L[3] = L[0];
    }
    if (n == 4)
    {
        L[4] = L[0];
    }
}

//! The share of the lobe that a quad covers, seen from P: the integral over the quad of a
//! clamped cosine transformed by inverse(Minv). With Minv the identity that is the Lambert
//! form factor, 1 / pi included; with a table's matrix it is the normalized GGX lobe, still
//! to be scaled by the table's amplitude.
//!
//! One-sided: the quad lights the side that cross(points[1] - points[0], points[3] - points[0])
//! points AWAY from, so the caller orders the points to match where its light shines.
float LTCEvaluateQuad(float3 N, float3 V, float3 P, float3x3 Minv, float3 points[4])
{
    // N as z, V in the x-z plane: the frame the tables were fitted in. Head-on there is no
    // such plane, and the lobe is symmetric about N, so any tangent serves.
    float3 T1 = V - N * dot(V, N);
    float  t1Len2 = dot(T1, T1);
    if (t1Len2 > 1e-8)
    {
        T1 *= rsqrt(t1Len2);
    }
    else
    {
        T1 = normalize(abs(N.z) < 0.999 ? cross(N, float3(0.0, 0.0, 1.0)) : float3(1.0, 0.0, 0.0));
    }
    float3 T2 = cross(N, T1);

    // Rows, so this is already world -> frame; the source transposes a column-built one.
    Minv = mul(Minv, float3x3(T1, T2, N));

    float3 L[5];
    L[0] = mul(Minv, points[0] - P);
    L[1] = mul(Minv, points[1] - P);
    L[2] = mul(Minv, points[2] - P);
    L[3] = mul(Minv, points[3] - P);
    L[4] = L[3];

    int n;
    LTCClipQuadToHorizon(L, n);
    if (n == 0)
    {
        return 0.0;
    }

    L[0] = normalize(L[0]);
    L[1] = normalize(L[1]);
    L[2] = normalize(L[2]);
    L[3] = normalize(L[3]);
    L[4] = normalize(L[4]);

    float sum = LTCIntegrateEdge(L[0], L[1]);
    sum += LTCIntegrateEdge(L[1], L[2]);
    sum += LTCIntegrateEdge(L[2], L[3]);
    if (n >= 4)
    {
        sum += LTCIntegrateEdge(L[3], L[4]);
    }
    if (n == 5)
    {
        sum += LTCIntegrateEdge(L[4], L[0]);
    }

    return max(0.0, sum);
}

#endif // SPARK_LIB_LTC_HLSLI
