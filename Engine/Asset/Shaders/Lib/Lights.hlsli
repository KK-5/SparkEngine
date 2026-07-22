// Light radiance library — turns a LightData record + shaded world position into the
// light direction L and the incident radiance arriving at that point (distance falloff
// and spot cones folded in). Takes LightData BY VALUE, so it is decoupled from where the
// record came from (deferred g_Lights StructuredBuffer, a forward loop, clustered, ...):
// the only dependency is the record layout, not the space0 binding.
#ifndef SPARK_LIB_LIGHTS_HLSLI
#define SPARK_LIB_LIGHTS_HLSLI

#include <Shaders/LightData.hlsli>

// Returns the incident radiance at worldPos for this light, and writes L (unit vector
// pointing from the surface toward the light). Scale a BRDF term by the return value.
float3 EvaluateLight(LightData light, float3 worldPos, out float3 L)
{
    // Directional: L is the negated shine direction, no attenuation.
    if (light.type == 0)
    {
        L = normalize(-light.direction);
        return light.color * light.intensity;
    }

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

    return light.color * light.intensity * attenuation;
}

#endif // SPARK_LIB_LIGHTS_HLSLI
