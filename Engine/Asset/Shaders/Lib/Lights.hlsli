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

//! 1 = lit, 0 = fully shadowed. The tile transform is already inside worldToShadowUV, so
//! nothing here knows the atlas layout.
float SampleShadow(int shadowIndex, float3 worldPos, float3 N)
{
    ShadowViewData sv = GetShadowView(shadowIndex);

    // Along the normal, not the light: a surface at a grazing angle is the one whose depth
    // spans a whole texel, and the offset has to grow with that span rather than with NoL.
    float3 p = worldPos + N * sv.normalOffset;

    float4 clip = mul(sv.worldToShadowUV, float4(p, 1.0));
    float3 uvz  = clip.xyz / clip.w;

    // Outside the light's frustum there is no depth to compare against. Unlit is wrong here
    // -- a directional box that does not cover the scene would shadow everything beyond it.
    if (uvz.z <= 0.0 || uvz.z >= 1.0)
    {
        return 1.0;
    }
    if (any(uvz.xy < sv.uvMinMax.xy) || any(uvz.xy > sv.uvMinMax.zw))
    {
        return 1.0;
    }

    // The tile is rendered inset by a texel, so the sampler's 2x2 footprint at the very edge
    // reaches only into that border -- cleared to 1.0, which compares as lit.
    return g_ShadowAtlas.SampleCmpLevelZero(g_ShadowSampler, uvz.xy, uvz.z - sv.depthBias);
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
        radiance *= SampleShadow(light.shadowIndex, worldPos, N);
    }
    return radiance;
}

#endif // SPARK_LIB_LIGHTS_HLSLI
