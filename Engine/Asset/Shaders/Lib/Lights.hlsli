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
#include <Shaders/Lib/LTC.hlsli>

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

// A rect light (type 3) has no single L to hand a BRDF: the response is integrated over the
// emitter, so this returns the unshadowed outgoing radiance outright and EvaluateLight /
// EvaluateBRDF are not involved. light.intensity is the emitter's radiance.
//
// The tables come in as parameters, as EnvBRDFLut takes its own; the caller checks
// HasAreaLightLut before passing them.
float3 EvaluateRectLight(LightData light, float3 worldPos, float3 N, float3 V,
                         float3 diffuseColor, float3 F0, float perceptualRoughness,
                         Texture2D ltc1, Texture2D ltc2, SamplerState lutSampler)
{
    float3 toLight = light.position - worldPos;

    // Behind the emitter, or past the same range window the point light uses. The integral
    // falls off with distance by itself; the window is only what makes the reach finite.
    float t = length(toLight) * light.invRange;
    float window = saturate(1 - t * t * t * t);
    if (dot(toLight, light.direction) >= 0.0 || window <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    // Ordered so the quad lights the side light.direction points to (see LTCEvaluateQuad).
    float3 ex = light.right * light.halfWidth;
    float3 ey = cross(light.direction, light.right) * light.halfHeight;
    float3 points[4];
    points[0] = light.position - ex + ey;
    points[1] = light.position + ex + ey;
    points[2] = light.position + ex - ey;
    points[3] = light.position - ex - ey;

    float2 uv = LTCTableUV(perceptualRoughness, saturate(dot(N, V)));
    float4 t1 = ltc1.SampleLevel(lutSampler, uv, 0.0);
    float4 t2 = ltc2.SampleLevel(lutSampler, uv, 0.0);

    const float3x3 identity = float3x3(1.0, 0.0, 0.0,  0.0, 1.0, 0.0,  0.0, 0.0, 1.0);
    float diffuse  = LTCEvaluateQuad(N, V, worldPos, identity, points);
    float specular = LTCEvaluateQuad(N, V, worldPos, LTCInverseMatrix(t1), points);

    // The lobe integrates to 1; t2.x is what the BRDF integrates to with F = 1 and t2.y the
    // part of that weighted by Schlick's (1 - VoH)^5, so this is F0 * x + (1 - F0) * y.
    float3 specularColor = F0 * t2.x + (1.0 - F0) * t2.y;

    return light.color * light.intensity * (window * window)
         * (diffuseColor * diffuse + specularColor * specular);
}

#endif // SPARK_LIB_LIGHTS_HLSLI
