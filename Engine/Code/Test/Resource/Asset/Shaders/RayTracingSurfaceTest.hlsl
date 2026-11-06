// Compile the complete hit-shading path in compute: explicit texture LOD must work
// without pixel derivatives, and all shared resources must retain the engine layout.
#include <Shaders/Lib/RayTracing/RayTracingSurface.hlsli>

RWStructuredBuffer<float4> g_Output : register(u0, space2);
SamplerState g_MatSampler : register(s0, space2);

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    RayDesc ray;
    ray.Origin = float3(id.xy, -10.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.001;
    ray.TMax = 1000.0;
    RayTracingHit hit;
    if (!TraceClosestOpaqueTriangle(ray, 0xff, hit))
    {
        g_Output[0] = 0.0;
        return;
    }

    RayTracingSurface surface = LoadRayTracingSurface(ray, hit);
    RayTracingMaterial material = EvaluateRayTracingMaterial(surface, g_MatSampler, float(id.z));
    const float3 V = -ray.Direction;
    const float3 L = normalize(float3(1.0, 1.0, -1.0));
    RayDesc shadow;
    shadow.Origin = surface.WorldPosition + surface.WorldGeometricNormal * 0.001;
    shadow.Direction = L;
    shadow.TMin = 0.0;
    shadow.TMax = 1000.0;
    const float visibility = TraceOpaqueVisibility(shadow, 0xff) ? 1.0 : 0.0;
    g_Output[0] = float4(EvaluateRayTracingBRDF(material, V, L) * visibility + material.Inputs.EmissiveColor, hit.T);
    g_Output[1] = float4(surface.WorldPosition, float(hit.InstanceIndex));
    g_Output[2] = float4(surface.WorldGeometricNormal, float(hit.PrimitiveIndex));
    g_Output[3] = float4(material.WorldNormal, material.Inputs.Roughness);
    g_Output[4] = float4(material.Inputs.BaseColor, material.Inputs.Metallic);
    g_Output[5] = float4(surface.MaterialParameters.TexCoord0, material.Inputs.AmbientOcclusion, material.Inputs.Opacity);
}
