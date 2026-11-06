#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Lib/RayTracing/RayTracingSurface.hlsli>

struct ScopeParameters
{
    uint viewIndex;
    uint debugMode;
};
#include <Shaders/ScopeBindings.hlsli>

SamplerState g_MaterialSampler : register(s0, space2);

struct VSOutput
{
    float4 Position : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.Position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float3 LinearToDisplay(float3 color)
{
    color = max(color, 0.0);
    return lerp(1.055 * pow(color, 1.0 / 2.4) - 0.055, 12.92 * color, color <= 0.0031308);
}

float4 PSMain(VSOutput input) : SV_Target0
{
    const ViewData view = GetView(g_Scope.viewIndex);
    // One deterministic sample at the pixel center. No jitter, history or denoising.
    const float2 uv = (input.Position.xy - view.viewRectMin.xy) * view.viewSizeAndInvSize.zw;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float4 nearPoint = mul(view.invViewProj, float4(ndc, 1.0, 1.0));
    RayDesc ray;
    ray.Origin = mul(view.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    ray.Direction = normalize(nearPoint.xyz / nearPoint.w - ray.Origin);
    ray.TMin = 0.001;
    ray.TMax = 20.0;

    RayTracingHit hit;
    if (!TraceClosestOpaqueTriangle(ray, 0xFF, hit))
    {
        return float4(0.02, 0.02, 0.02, 1.0);
    }
    const RayTracingSurface surface = LoadRayTracingSurface(ray, hit);
    const RayTracingMaterial material = EvaluateRayTracingMaterial(surface, g_MaterialSampler, 0.0);
    float3 color;
    if (g_Scope.debugMode == 1)
    {
        color = LinearToDisplay(material.Inputs.BaseColor);
    }
    else if (g_Scope.debugMode == 2)
    {
        color = material.WorldNormal * 0.5 + 0.5;
    }
    else if (g_Scope.debugMode == 3)
    {
        color = surface.WorldGeometricNormal * 0.5 + 0.5;
    }
    else if (g_Scope.debugMode == 4)
    {
        const float2 texCoord = surface.MaterialParameters.TexCoord0;
        const float checker = fmod(floor(texCoord.x * 8.0) + floor(texCoord.y * 8.0), 2.0);
        color = float3(frac(texCoord), checker);
    }
    else if (g_Scope.debugMode == 5)
    {
        color = float3(material.Inputs.Roughness, material.Inputs.Metallic, material.Inputs.Specular);
    }
    else
    {
        // Diagnostic camera headlight: inspect the shared material/BRDF without an
        // indirect-light integrator, light sampling or shadow rays hiding bad attributes.
        const float3 V = -ray.Direction;
        const float3 radiance = material.Inputs.EmissiveColor + EvaluateRayTracingBRDF(material, V, V) * 3.14159265;
        color = LinearToDisplay(radiance / (1.0 + radiance));
    }
    return float4(color, 1.0);
}
