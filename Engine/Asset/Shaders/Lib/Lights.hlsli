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
#include <Shaders/Lib/Shadow/ShadowSampling.hlsli>

// Shadow atlas, one tile per shadow-casting light. Viewed as R32_FLOAT. This declaration is
// the whole of the shadow binding contract — Lib/Shadow/ takes both as arguments and holds
// no opinion about where they live.
Texture2D              g_ShadowAtlas   : register(t5, space2);
SamplerComparisonState g_ShadowSampler : register(s0, space2);

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
        radiance *= SampleShadow(GetShadowView(row), worldPos, N,
                                 g_ShadowAtlas, g_ShadowSampler, g_ShadowAtlasTexelSize);
    }
    return radiance;
}

#endif // SPARK_LIB_LIGHTS_HLSLI
