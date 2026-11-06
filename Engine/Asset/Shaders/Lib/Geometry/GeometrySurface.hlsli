// Instance triangle geometry and barycentric surface interpolation. Independent of ray
// queries and texture sampling; callers may obtain barycentrics from hits or sampling.
#ifndef SPARK_LIB_GEOMETRY_SURFACE_HLSLI
#define SPARK_LIB_GEOMETRY_SURFACE_HLSLI

#include <Shaders/InstanceBindings.hlsli>
#include <Shaders/Lib/Geometry/MeshGeometry.hlsli>
#include <Shaders/Material/MaterialParameters.hlsli>

struct WorldTriangle
{
    float3 Position0;
    float3 Edge1; // position1 - position0
    float3 Edge2; // position2 - position0
    float3 GeometricNormal; // cross(Edge1, Edge2), normalized; never flipped toward a ray
    float Area;
};

WorldTriangle TransformMeshTriangle(InstanceData instance, MeshTriangle meshTriangle)
{
    WorldTriangle world;
    world.Position0 = mul(instance.Model, float4(meshTriangle.Vertex0.Position, 1.0)).xyz;
    // Transform local differences rather than subtracting translated world positions.
    world.Edge1 = mul((float3x3)instance.Model, meshTriangle.Vertex1.Position - meshTriangle.Vertex0.Position);
    world.Edge2 = mul((float3x3)instance.Model, meshTriangle.Vertex2.Position - meshTriangle.Vertex0.Position);
    const float3 crossEdges = cross(world.Edge1, world.Edge2);
    const float twiceArea = length(crossEdges);
    world.Area = 0.5 * twiceArea;
    // Degenerate triangles have zero area and zero geometric normal. Sampling callers
    // must reject them before dividing by area or constructing an area PDF.
    world.GeometricNormal = twiceArea > 0.0 ? crossEdges / twiceArea : float3(0.0, 0.0, 0.0);
    return world;
}

struct GeometrySurface
{
    float3 WorldPosition;
    // Preserves transformed index winding, including mirror transforms. This is not
    // automatically the authored normal direction or the ray query front-face flag.
    float3 WorldTriangleNormal;
    // Initially WorldTriangleNormal; FaceGeometrySurface orients it for shading/offsets.
    float3 WorldGeometricNormal;
    MaterialPixelParameters MaterialParameters;
};

GeometrySurface InterpolateGeometrySurface(
    InstanceData instance, MeshTriangle meshTriangle, float2 barycentrics)
{
    const float3 weights = GetTriangleBarycentricWeights(barycentrics);
    const WorldTriangle world = TransformMeshTriangle(instance, meshTriangle);
    const float3 localNormal = meshTriangle.Vertex0.Normal * weights.x
        + meshTriangle.Vertex1.Normal * weights.y + meshTriangle.Vertex2.Normal * weights.z;
    const float4 localTangent = meshTriangle.Vertex0.Tangent * weights.x
        + meshTriangle.Vertex1.Tangent * weights.y + meshTriangle.Vertex2.Tangent * weights.z;
    const float3 worldNormal = normalize(mul((float3x3)instance.NormalMatrix, localNormal));
    const float3 worldTangent = mul((float3x3)instance.Model, localTangent.xyz);
    const float orientation = determinant((float3x3)instance.Model) < 0.0 ? -1.0 : 1.0;

    GeometrySurface surface;
    surface.WorldPosition = world.Position0 + barycentrics.x * world.Edge1 + barycentrics.y * world.Edge2;
    surface.WorldTriangleNormal = world.GeometricNormal;
    surface.WorldGeometricNormal = world.GeometricNormal;
    surface.MaterialParameters = (MaterialPixelParameters)0;
    surface.MaterialParameters.WorldVertexNormal = worldNormal;
    surface.MaterialParameters.WorldTangent = float4(worldTangent, (localTangent.w < 0.0 ? -1.0 : 1.0) * orientation);
    surface.MaterialParameters.TexCoord0 = meshTriangle.Vertex0.TexCoord0 * weights.x
        + meshTriangle.Vertex1.TexCoord0 * weights.y + meshTriangle.Vertex2.TexCoord0 * weights.z;
    surface.MaterialParameters.MaterialIndex = instance.MaterialIndex;
    // No viewing direction is assumed: preserve the authored shading frame by default.
    surface.MaterialParameters.TwoSidedSign = 1.0;
    surface.MaterialParameters.WorldNormal = worldNormal;
    return surface;
}

// No TLAS or RayTracingHit required. The instance's geometry must be ready and the
// primitive index and barycentrics must address a valid point of its current mesh.
GeometrySurface LoadGeometrySurface(uint instanceIndex, uint primitiveIndex, float2 barycentrics)
{
    const InstanceData instance = GetInstanceData(instanceIndex);
    const GeometryData geometry = g_Geometries[instance.GeometryIndex];
    const MeshTriangle meshTriangle = LoadMeshTriangle(geometry, primitiveIndex);
    return InterpolateGeometrySurface(instance, meshTriangle, barycentrics);
}

// outgoingDirection points away from the surface (e.g. -ray.Direction). Triangle
// winding is preserved independently of the face-forward geometric/shading frame.
void FaceGeometrySurface(inout GeometrySurface surface, float3 outgoingDirection)
{
    surface.WorldGeometricNormal = dot(surface.WorldTriangleNormal, outgoingDirection) < 0.0
        ? -surface.WorldTriangleNormal : surface.WorldTriangleNormal;
    surface.MaterialParameters.TwoSidedSign = dot(
        surface.MaterialParameters.WorldVertexNormal, surface.WorldGeometricNormal) < 0.0 ? -1.0 : 1.0;
}

#endif // SPARK_LIB_GEOMETRY_SURFACE_HLSLI
