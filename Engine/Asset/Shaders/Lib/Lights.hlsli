// Light radiance library — turns a LightData record + shaded world position into the light
// direction L and the incident radiance arriving at that point, with distance falloff and
// spot cone folded in.
//
// Visibility is NOT folded in: it comes from the ShadowMask, a screen-space signal whose
// source can be the raster atlas or ray tracing, and the caller multiplies it. So this file
// binds nothing and depends only on the record layout.
#ifndef SPARK_LIB_LIGHTS_HLSLI
#define SPARK_LIB_LIGHTS_HLSLI

#include <Shaders/LightData.hlsli>
#include <Shaders/SceneBindings.hlsli>

// Returns the unshadowed incident radiance at worldPos for this light, and writes L (unit
// vector pointing from the surface toward the light).
float3 EvaluateLight(LightData light, float3 worldPos, out float3 L)
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
    return radiance;
}

#endif // SPARK_LIB_LIGHTS_HLSLI
