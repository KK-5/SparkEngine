// Engine mesh hit reconstruction and default-lit material evaluation. Shared by reference
// tracing, reflections and GI. See Render/RayTracing/README.md for binding/graph contracts.
#ifndef SPARK_LIB_RAY_TRACING_SURFACE_HLSLI
#define SPARK_LIB_RAY_TRACING_SURFACE_HLSLI

#include <Shaders/Lib/RayTracing/RayTracing.hlsli>
#include <Shaders/InstanceBindings.hlsli>
#include <Shaders/GeometryBindings.hlsli>
#include <Shaders/Material/MaterialTemplate.hlsli>
#include <Shaders/Lib/ShadingCommon.hlsli>
#include <Shaders/Lib/BRDF/BRDF.hlsli>

// Mirrors Resource::StandardVertex in Resource/Model/VertexLayout.h. The mesh compiler and
// pool enforce this layout: float3 position, float3 normal, float4 tangent, float2 UV0.
struct RayTracingVertex
{
    float3 Position;
    float3 Normal;
    float4 Tangent;
    float2 TexCoord0;
};

RayTracingVertex LoadRayTracingVertex(uint vertexIndex)
{
    const uint address = vertexIndex * 48u;
    RayTracingVertex vertex;
    vertex.Position = asfloat(g_MeshGeometry.Load3(address));
    vertex.Normal = asfloat(g_MeshGeometry.Load3(address + 12u));
    vertex.Tangent = asfloat(g_MeshGeometry.Load4(address + 24u));
    vertex.TexCoord0 = asfloat(g_MeshGeometry.Load2(address + 40u));
    return vertex;
}

// ByteAddressBuffer.Load requires a 4-byte-aligned address. Round down to the word
// containing this byte, preserving indices that start in its upper 16 bits.
uint AlignDownTo4Bytes(uint byteOffset)
{
    return byteOffset & ~3u;
}

// byteOffsetInWord is 0 or 2: select the lower or upper UINT16 of a loaded 32-bit word.
uint ExtractUint16FromWord(uint word, uint byteOffsetInWord)
{
    const uint bitOffset = byteOffsetInWord * 8u;
    return (word >> bitOffset) & 0xffffu;
}

// Returns pool-global vertex indices. UINT16 allocations may start in the upper half of
// a word, so each index is extracted from its aligned 32-bit load independently.
uint LoadRayTracingIndex(GeometryData geometry, uint localIndex)
{
    uint index = localIndex;
    if (geometry.IndexByteStride != 0)
    {
        const uint address = (geometry.FirstIndex + localIndex) * geometry.IndexByteStride;
        const uint alignedAddress = AlignDownTo4Bytes(address);
        const uint word = g_MeshGeometry.Load(alignedAddress);
        const uint byteOffsetInWord = address - alignedAddress;
        index = geometry.IndexByteStride == 2u ? ExtractUint16FromWord(word, byteOffsetInWord) : word;
    }
    return uint(geometry.VertexOffset) + index;
}

struct RayTracingSurface
{
    float3 WorldPosition;
    // Faces the incoming ray. Use this for geometric hemisphere tests and ray-origin
    // offsets, rather than the normal map's shading normal.
    float3 WorldGeometricNormal;
    MaterialPixelParameters MaterialParameters;
};

// Precondition: a successful hit on this frame's engine TLAS, GeometryIndex == 0. Each
// engine BLAS uses one mesh primitive; custom BLAS input layouts need another decoder.
// Both sides are reconstructed: queries intentionally have no back-face culling.
RayTracingSurface LoadRayTracingSurface(RayDesc ray, RayTracingHit hit)
{
    const InstanceData instance = GetInstanceData(hit.InstanceIndex);
    const GeometryData geometry = g_Geometries[instance.GeometryIndex];
    const uint first = hit.PrimitiveIndex * 3u;
    const RayTracingVertex v0 = LoadRayTracingVertex(LoadRayTracingIndex(geometry, first));
    const RayTracingVertex v1 = LoadRayTracingVertex(LoadRayTracingIndex(geometry, first + 1u));
    const RayTracingVertex v2 = LoadRayTracingVertex(LoadRayTracingIndex(geometry, first + 2u));
    const float3 weights = float3(1.0 - hit.Barycentrics.x - hit.Barycentrics.y, hit.Barycentrics);

    const float3 edge1 = mul((float3x3)instance.Model, v1.Position - v0.Position);
    const float3 edge2 = mul((float3x3)instance.Model, v2.Position - v0.Position);
    float3 geometricNormal = normalize(cross(edge1, edge2));
    if (dot(geometricNormal, ray.Direction) > 0.0)
    {
        geometricNormal = -geometricNormal;
    }

    const float3 localNormal = v0.Normal * weights.x + v1.Normal * weights.y + v2.Normal * weights.z;
    const float4 localTangent = v0.Tangent * weights.x + v1.Tangent * weights.y + v2.Tangent * weights.z;
    const float3 worldNormal = normalize(mul((float3x3)instance.NormalMatrix, localNormal));
    const float3 worldTangent = mul((float3x3)instance.Model, localTangent.xyz);
    // Mirrored instances reverse tangent-frame handedness. Nonuniform scale takes normals
    // through the inverse transpose, tangents/edges through Model, just like rasterization.
    const float orientation = determinant((float3x3)instance.Model) < 0.0 ? -1.0 : 1.0;

    RayTracingSurface surface;
    surface.WorldPosition = ray.Origin + hit.T * ray.Direction;
    surface.WorldGeometricNormal = geometricNormal;
    surface.MaterialParameters = (MaterialPixelParameters)0;
    surface.MaterialParameters.WorldVertexNormal = worldNormal;
    surface.MaterialParameters.WorldTangent = float4(worldTangent, (localTangent.w < 0.0 ? -1.0 : 1.0) * orientation);
    surface.MaterialParameters.TexCoord0 = v0.TexCoord0 * weights.x + v1.TexCoord0 * weights.y + v2.TexCoord0 * weights.z;
    surface.MaterialParameters.MaterialIndex = instance.MaterialIndex;
    surface.MaterialParameters.TwoSidedSign = dot(worldNormal, geometricNormal) < 0.0 ? -1.0 : 1.0;
    surface.MaterialParameters.WorldNormal = worldNormal;
    return surface;
}

struct RayTracingMaterial
{
    PixelMaterialInputs Inputs; // roughness remains unclamped; AO is data, not applied to traced light
    float3 WorldNormal;
    float3 DiffuseColor;
    float3 SpecularColor;
};

RayTracingMaterial EvaluateRayTracingMaterial(
    RayTracingSurface surface, SamplerState materialSampler, float mipLevel)
{
    MaterialPixelParameters parameters = surface.MaterialParameters;
    RayTracingMaterial material;
    CalcMaterialParametersLevel(parameters, material.Inputs, materialSampler, mipLevel);
    material.WorldNormal = parameters.WorldNormal;
    material.DiffuseColor = ComputeDiffuseColor(material.Inputs.BaseColor, material.Inputs.Metallic);
    material.SpecularColor = ComputeF0(material.Inputs.Specular, material.Inputs.BaseColor, material.Inputs.Metallic);
    return material;
}

// Same BRDF * NoL convention as deferred lighting. V and L point away from the surface.
// This is a surface response, not a complete lighting integral or a sampling PDF.
float3 EvaluateRayTracingBRDF(RayTracingMaterial material, float3 V, float3 L)
{
    // The reflection BRDF has support only above its shading hemisphere. Besides avoiding
    // wasted work, this prevents normalize(V + L) from producing NaN when L == -V.
    if (dot(material.WorldNormal, V) <= 0.0 || dot(material.WorldNormal, L) <= 0.0)
    {
        return 0.0;
    }
    return EvaluateBRDF(material.WorldNormal, V, L, material.DiffuseColor, material.SpecularColor,
                        max(material.Inputs.Roughness, 0.045));
}

#endif // SPARK_LIB_RAY_TRACING_SURFACE_HLSLI
