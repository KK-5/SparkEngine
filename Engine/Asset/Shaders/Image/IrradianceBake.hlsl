// IrradianceBake.hlsl — diffuse irradiance E(N), cosine-weighted importance sampling.
//
// One dispatch: Dispatch(faceRes, faceRes, 6). Each output texel is a normal N; we estimate
//     E(N) = integral over the hemisphere of L_i(L) * (N·L) dω
// by sampling directions with pdf p(L) = cosθ/π. The Lambert cosine and the pdf's cosine
// cancel, so the Monte Carlo estimator collapses to  E ≈ (π/N) · Σ L_i  — just an average
// of sampled radiances times π. No sinθ Jacobian, no explicit cosθ weight: both are folded
// into the sampling distribution.
//
// Source is the sky cube as a TextureCube SRV (transitioned to shader-read before this
// runs), sampled at a low mip so small bright sources don't flicker across neighbouring N.
//
// Stores PURE E (option A): the 1/π normalization stays in Fd_Lambert() in the BRDF lib, it
// is NOT baked here. Getting this wrong scales diffuse by π and is near-invisible after
// tonemap, so it is fixed by convention, not by eye.

#include <Shaders/Lib/Cubemap/CubemapUtils.hlsli>
#include <Shaders/Lib/Sample/Sample.hlsli>

TextureCube<float4>      g_SrcCube    : register(t0, space0);
SamplerState             g_Sampler    : register(s0, space0);
RWTexture2DArray<float4> g_Irradiance : register(u0, space0);

static const float PI           = 3.14159265358979323846;
static const float TWO_PI       = 6.28318530717958647692;
static const uint  SAMPLE_COUNT = 2048u;

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height, faceCount;
    g_Irradiance.GetDimensions(width, height, faceCount);
    if (id.x >= width || id.y >= height || id.z >= faceCount)
    {
        return;
    }

    float3 N = CubeTexelDirection(id.z, id.xy, width);

    // Orthonormal tangent frame with N as +z. abs(N.z) < 0.999 avoids the degenerate cross
    // when N is (near) parallel to the reference up.
    float3 up    = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 right = normalize(cross(up, N));
    up           = normalize(cross(N, right));

    // Cosine convolution is a strong low-pass, so sampling a low-res source mip (~32px face)
    // kills fireflies at no visible cost. Derived from the source — no constant needed.
    uint srcW, srcH, srcLevels;
    g_SrcCube.GetDimensions(0, srcW, srcH, srcLevels);
    float srcLod = clamp(log2(float(srcW) / 32.0), 0.0, float(srcLevels - 1));

    float3 sum = float3(0, 0, 0);
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 xi = Hammersley(i, SAMPLE_COUNT);

        // Cosine-weighted hemisphere sample in tangent space (z along N):
        //   r = sqrt(u1), φ = 2π·u2  ⇒  cosθ = sqrt(1-u1),  pdf = cosθ/π.
        float  r   = sqrt(xi.x);
        float  phi = TWO_PI * xi.y;
        float3 tangentDir = float3(r * cos(phi), r * sin(phi), sqrt(1.0 - xi.x));
        float3 worldDir   = tangentDir.x * right + tangentDir.y * up + tangentDir.z * N;

        // Estimator term is L·cosθ/pdf = L·cosθ/(cosθ/π) = π·L → accumulate L, scale once.
        sum += g_SrcCube.SampleLevel(g_Sampler, worldDir, srcLod).rgb;
    }

    float3 E = sum * (PI / float(SAMPLE_COUNT));
    g_Irradiance[id] = float4(E, 1.0);
}