#include <Shaders/Lib/BRDF/Constants.hlsli>
#include <Shaders/Lib/LightRadiance.hlsli>
#include <Shaders/SceneBindings.hlsli>

struct ScopeParameters
{
    uint nodeBase;
    uint leafBase;
    uint leafCount;
    float sceneRadiusSquared;
};

#include <Shaders/ScopeBindings.hlsli>

RWStructuredBuffer<float> g_WeightTree : register(u0, space2);

float LightLuminance(float3 rgb)
{
    return max(dot(rgb, float3(0.2126, 0.7152, 0.0722)), 0.0);
}

float EstimateAnalyticLightPower(AnalyticLightData light)
{
    if (light.type == LIGHT_TYPE_RECT)
    {
        const float area = 4.0 * light.halfWidth * light.halfHeight;
        return area > 0.0 ? PI * area * LightLuminance(light.color * light.intensity) : 0.0;
    }

    const float intensity = LightLuminance(light.color * light.intensity);
    if (light.type == LIGHT_TYPE_DIRECTIONAL)
    {
        return PI * g_Scope.sceneRadiusSquared * intensity;
    }
    if (light.type == LIGHT_TYPE_POINT)
    {
        return 4.0 * PI * intensity;
    }
    if (light.type == LIGHT_TYPE_SPOT)
    {
        const float capWidth = 1.0 - light.cosOuter;
        const float coneWidth = SpotConeCosineWidth(light);
        const float rampWidth = min(capWidth, coneWidth);
        const float rampEnd = rampWidth / coneWidth;
        // Integrate the squared ramp and the fully lit part over cos(theta).
        const float fullCone = capWidth - rampWidth;
        return 2.0 * PI * (fullCone + rampWidth * rampEnd * rampEnd / 3.0) * intensity;
    }
    return 0.0;
}

float EstimateEmissiveLightPower(EmissiveLightData light)
{
    // The local area alone cannot recover triangle orientations. Use the mean area scale
    // of the three coordinate planes, which is exact for a uniform scale.
    const float3x3 model = (float3x3)light.model;
    const float3 x = mul(model, float3(1.0, 0.0, 0.0));
    const float3 y = mul(model, float3(0.0, 1.0, 0.0));
    const float3 z = mul(model, float3(0.0, 0.0, 1.0));
    const float areaScale = (length(cross(x, y)) + length(cross(y, z)) + length(cross(z, x))) / 3.0;

    // Direct lighting treats emissive meshes as two-sided. The material factor estimates
    // emission here, while the actual lighting sample evaluates the emissive texture.
    return 2.0 * PI * light.geometry.LocalSurfaceArea * areaScale * LightLuminance(light.color * light.intensity);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_Scope.nodeBase)
    {
        return;
    }

    const uint node = g_Scope.nodeBase + id.x;
    if (g_Scope.nodeBase != g_Scope.leafBase)
    {
        g_WeightTree[node] = g_WeightTree[2u * node] + g_WeightTree[2u * node + 1u];
        return;
    }

    float weight = 0.0;
    if (id.x < g_Scope.leafCount)
    {
        const LightData light = GetLight(id.x);
        if (light.type == LIGHT_TYPE_EMISSIVE_MESH)
        {
            weight = EstimateEmissiveLightPower(DecodeEmissiveLight(light));
        }
        else if (IsAnalyticLight(light.type))
        {
            weight = EstimateAnalyticLightPower(DecodeAnalyticLight(light));
        }
    }
    g_WeightTree[node] = weight;
}
