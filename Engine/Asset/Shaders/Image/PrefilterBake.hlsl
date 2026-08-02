// PrefilterBake.hlsl — specular split-sum prefilter. One dispatch per mip, each a fixed
// roughness (mip N -> N/(mipLevels-1)). GGX importance sampling around R=V=N (Karis), with
// per-sample solid-angle source-mip selection to suppress fireflies at high roughness.

#include <Shaders/Lib/Cubemap/CubemapUtils.hlsli>
#include <Shaders/Lib/Sample/Sample.hlsli>
#include <Shaders/Lib/BRDF/Distribution.hlsli>

TextureCube<float4>      g_SrcCube     : register(t0, space0);
SamplerState             g_Sampler     : register(s0, space0);
RWTexture2DArray<float4> g_Prefiltered : register(u0, space0);

cbuffer PrefilterParams : register(b0, space0)
{
    float g_Roughness;
    uint  g_SampleCount;
};

static const float TWO_PI = 6.28318530717958647692;

float3 ImportanceSampleGGX(float2 xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    // Guard the a=0 mirror singularity: xi.y can round to exactly 1.0 -> 0/0 = NaN, which
    // poisons the whole sum. max()/saturate keep cosThetaH finite (a=0 still gives H = N).
    float denom = 1.0 + (a * a - 1.0) * xi.y;
    float cosThetaH = sqrt(saturate((1.0 - xi.y) / max(denom, 1e-8)));
    float sinThetaH = sqrt(saturate(1.0 - cosThetaH * cosThetaH));
    float phi = TWO_PI * xi.x;
    float3 tangentDir = float3(sinThetaH * cos(phi), sinThetaH * sin(phi), cosThetaH);

    float3 up    = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 right = normalize(cross(up, N));
    up           = normalize(cross(N, right));

    return tangentDir.x * right + tangentDir.y * up + tangentDir.z * N;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height, faceCount;
    g_Prefiltered.GetDimensions(width, height, faceCount);
    if (id.x >= width || id.y >= height || id.z >= faceCount)
    {
        return;
    }

    float3 N = CubeTexelDirection(id.z, id.xy, width);
    float3 V = N;
    // float3 R = N;   Karis: N = V = R

    uint srcW, srcH, srcLevels;
    g_SrcCube.GetDimensions(0, srcW, srcH, srcLevels);
    float srcTexelSolidAngle = 4 * PI / (6 * srcW * srcW);

    float3 color = float3(0, 0, 0);
    float totalWeight = 0;
    for (uint i = 0u; i < g_SampleCount; ++i)
    {
        float2 xi = Hammersley(i, g_SampleCount);
        float3 H = ImportanceSampleGGX(xi, N, g_Roughness);
        float3 L = -reflect(V, H);
        float NoL = dot(N, L);
        if (NoL <= 0)
        {
            continue;
        }

        float NoH = max(dot(N,H), 0);
        float VoH = max(dot(V,H), 0);

        // Real branch, NOT a ternary: at roughness 0, D_GGX(NoH=1, 0) is 0/0 = NaN, and a
        // ternary still evaluates the dead branch (NaN*0 poisons the select -> black).
        float mip = 0.0;
        if (g_Roughness > 0.0)
        {
            float pdf = D_GGX(NoH, g_Roughness * g_Roughness) * NoH / (4.0 * VoH) + 1e-4;
            float saSample = 1.0 / (g_SampleCount * pdf + 1e-4);
            mip = clamp(0.5 * log2(saSample / srcTexelSolidAngle), 0.0, srcLevels - 1);
        }

        color += g_SrcCube.SampleLevel(g_Sampler, L, mip).rgb * NoL;
        totalWeight += NoL;
    }

    g_Prefiltered[id] = float4(color / max(totalWeight, 1e-4), 1.0);
}

