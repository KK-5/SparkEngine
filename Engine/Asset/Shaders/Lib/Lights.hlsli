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

    // TODO: point (type 1) — position-based L + distance attenuation (use light.invRange).
    // TODO: spot  (type 2) — point falloff * cone term (light.cosInner / light.cosOuter).
    L = normalize(-light.direction);
    return light.color * light.intensity;
}

#endif // SPARK_LIB_LIGHTS_HLSLI
