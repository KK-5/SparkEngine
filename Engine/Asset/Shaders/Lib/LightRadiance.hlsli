// Shared analytic-light direction and attenuation, independent of scene bindings.
#ifndef SPARK_LIB_LIGHT_RADIANCE_HLSLI
#define SPARK_LIB_LIGHT_RADIANCE_HLSLI

#include <Shaders/LightData.hlsli>

float3 DirectionToLight(LightData light, float3 worldPos)
{
    if (light.type == 0)
    {
        return normalize(-light.direction);
    }
    const float3 toLight = light.position - worldPos;
    return toLight / sqrt(max(dot(toLight, toLight), 1e-8));
}

// The engine's finite-range window. Rect lights use their center distance too;
// their geometric distance falloff is accounted for by the area integral/PDF.
float EvaluateLightRangeWindow(LightData light, float3 worldPos)
{
    const float t = length(light.position - worldPos) * light.invRange;
    const float window = saturate(1.0 - t * t * t * t);
    return window * window;
}

// Directional, point and spot only. Rect lights must integrate/sample their area.
// Returns unoccluded incident radiance; L points from the receiver to the light.
float3 EvaluateLight(LightData light, float3 worldPos, out float3 L)
{
    if (light.type == 0)
    {
        L = DirectionToLight(light, worldPos);
        return light.color * light.intensity;
    }

    const float3 toLight = light.position - worldPos;
    const float dist2 = dot(toLight, toLight);
    L = DirectionToLight(light, worldPos);
    float attenuation = EvaluateLightRangeWindow(light, worldPos) / max(dist2, 1e-4);
    if (light.type == 2)
    {
        const float cosAngle = dot(light.direction, -L);
        const float cone = saturate((cosAngle - light.cosOuter) / max(light.cosInner - light.cosOuter, 1e-4));
        attenuation *= cone * cone;
    }
    return light.color * light.intensity * attenuation;
}

#endif // SPARK_LIB_LIGHT_RADIANCE_HLSLI
