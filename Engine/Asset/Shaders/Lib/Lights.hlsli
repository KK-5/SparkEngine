// Light radiance library — turns a LightData record + shaded world position into the light
// direction L and the incident radiance arriving at that point, with distance falloff, spot
// cone and shadowing all folded in. A caller scales its BRDF term by the return value and
// has nothing left to remember.
//
// Shadowing is what makes this NOT a self-contained record-layout dependency: it reads
// g_ShadowViews (space0, via SceneBindings) and the atlas pair declared below at space2.
// Any shader that includes this must therefore bind those, and must leave t5 / s0 in its
// per-pass space free.
#ifndef SPARK_LIB_LIGHTS_HLSLI
#define SPARK_LIB_LIGHTS_HLSLI

#include <Shaders/LightData.hlsli>
#include <Shaders/SceneBindings.hlsl>

// Shadow atlas, one tile per shadow-casting light. Viewed as R32_FLOAT.
Texture2D              g_ShadowAtlas   : register(t5, space2);
SamplerComparisonState g_ShadowSampler : register(s0, space2);

//! Nine points on the unit disk, Vogel spiral. A square tap grid is anisotropic — a 45 degree
//! edge gets a 41% wider ramp than an axis-aligned one — which reads as facets around a curved
//! shadow rather than as softness.
static const float2 kShadowDisk[9] =
{
    float2( 0.2357,  0.0000), float2(-0.3010,  0.2758), float2( 0.0461, -0.5250),
    float2( 0.3792,  0.4951), float2(-0.6964, -0.1232), float2( 0.6592, -0.4202),
    float2(-0.2206,  0.8207), float2(-0.4209, -0.8101), float2( 0.9128,  0.3336),
};

//! Fraction of the kernel that is lit. radiusTexels is a parameter rather than a constant
//! because PCSS reuses this step as-is, with a radius its blocker search computes per pixel.
//!
//! Every tap is clamped into the tile, so no radius can reach a neighbouring tile -- whose
//! depth belongs to an unrelated light and compares to an arbitrary result. Clamping rather
//! than a border sized to the kernel is also what survives VSM, where pages that are adjacent
//! in virtual space are scattered in physical memory and no fixed border can bridge them.
float PCF(float2 uv, float z, float radiusTexels, float4 uvMinMax)
{
    const float step = radiusTexels * g_ShadowAtlasTexelSize;

    float sum = 0.0;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        float2 tap = clamp(uv + kShadowDisk[i] * step, uvMinMax.xy, uvMinMax.zw);
        sum += g_ShadowAtlas.SampleCmpLevelZero(g_ShadowSampler, tap, z);
    }
    return sum * (1.0 / 9.0);
}

//! face = axis * 2 + negative, for a vector pointing from the light at what it lights.
//! ShadowViewSystem::CubeFaceDirection is the same encoding read the other way, and the two
//! must agree — nothing else about a face is shared, its orientation included.
uint CubeFaceIndex(float3 v)
{
    float3 a    = abs(v);
    uint   axis = (a.x >= a.y && a.x >= a.z) ? 0 : (a.y >= a.z ? 1 : 2);
    return axis * 2 + (v[axis] < 0.0 ? 1 : 0);
}

//! 1 = lit, 0 = fully shadowed. The tile transform is already inside worldToShadowUV, so
//! nothing here knows the atlas layout.
float SampleShadow(int shadowIndex, float3 worldPos, float3 N)
{
    ShadowViewData sv = GetShadowView(shadowIndex);

    float3 p = worldPos + N * sv.normalOffset;

    float4 clip = mul(sv.worldToShadowUV, float4(p, 1.0));
    float3 uvz  = clip.xyz / clip.w;

    if (uvz.z <= 0.0 || uvz.z >= 1.0)
    {
        return 1.0;
    }
    if (any(uvz.xy < sv.uvMinMax.xy) || any(uvz.xy > sv.uvMinMax.zw))
    {
        return 1.0;
    }

    return PCF(uvz.xy, uvz.z - sv.depthBias, sv.pcfRadiusTexels, sv.uvMinMax);
}

// Returns the incident radiance at worldPos for this light, already shadowed, and writes L
// (unit vector pointing from the surface toward the light). N is the shading normal, used
// only to offset the shadow lookup off the surface.
float3 EvaluateLight(LightData light, float3 worldPos, float3 N, out float3 L)
{
    float3 radiance;

    // Directional: L is the negated shine direction, no attenuation.
    if (light.type == 0)
    {
        L = normalize(-light.direction);
        radiance = light.color * light.intensity;
    }
    else
    {
        float3 toLight = light.position - worldPos;
        float dist2 = dot(toLight, toLight);
        float dist = sqrt(max(dist2, 1e-8));
        L = toLight / dist;

        float attenuation = 1.0 / max(dist2, 1e-4);
        float t = dist * light.invRange;
        float window = saturate(1 - t * t * t * t);
        attenuation *= window * window;

        if (light.type == 2)
        {
            float cosAngle = dot(light.direction, -L);
            float cone     = saturate((cosAngle - light.cosOuter) / max(light.cosInner - light.cosOuter, 1e-4));
            attenuation *= cone * cone;
        }

        radiance = light.color * light.intensity * attenuation;
    }

    // -1 covers both "this light does not cast" and the warmup frames before the atlas
    // exists: ShadowViewSystem hands out no tile until then.
    if (light.shadowIndex >= 0)
    {
        int row = light.shadowIndex;
        if (light.shadowFaceCount > 1)
        {
            row += int(CubeFaceIndex(worldPos - light.position));
        }
        radiance *= SampleShadow(row, worldPos, N);
    }
    return radiance;
}

#endif // SPARK_LIB_LIGHTS_HLSLI
