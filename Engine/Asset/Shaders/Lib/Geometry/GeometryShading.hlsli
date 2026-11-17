// Geometry-only shading helpers. No bindings or material evaluation, so raster,
// ray-hit and visibility-buffer callers can share the same calculations.
#ifndef SPARK_LIB_GEOMETRY_SHADING_HLSLI
#define SPARK_LIB_GEOMETRY_SHADING_HLSLI

// Pass derivatives of the primitive's interpolated world position, not samples
// from a depth texture: neighbouring depth texels can belong to other surfaces.
float3 ComputeGeometricNormal(float3 positionDdx, float3 positionDdy,
                              float3 fallbackNormal, float3 outgoingDirection)
{
    float3 normal = cross(positionDdx, positionDdy);
    const float lengthSquared = dot(normal, normal);
    normal = lengthSquared > 0.0 ? normal * rsqrt(lengthSquared) : fallbackNormal;
    return dot(normal, outgoingDirection) < 0.0 ? -normal : normal;
}

// Hanika, "Hacking the Shadow Terminator", Ray Tracing Gems II, Listing 4-1:
// https://jo.dreggn.org/home/2021_terminator.pdf
// Positions and unit vertex normals must share one space. Normals must face the
// side being shaded and must NOT include normal-map perturbations. The returned
// displacement is only for ray origins, not light PDFs, BRDFs or GBuffer depth.
float3 ComputeShadowOriginOffset(float3 position, float3 positions[3], float3 normals[3])
{
    const float3 edge1 = positions[1] - positions[0];
    const float3 edge2 = positions[2] - positions[0];
    const float3 areaNormal = cross(edge1, edge2);
    const float areaSquared = dot(areaNormal, areaNormal);
    if (areaSquared <= 0.0)
    {
        return 0.0;
    }

    const float3 relativePosition = position - positions[0];
    float3 weights;
    weights.y = dot(cross(relativePosition, edge2), areaNormal) / areaSquared;
    weights.z = dot(cross(edge1, relativePosition), areaNormal) / areaSquared;
    weights.x = 1.0 - weights.y - weights.z;
    // Contain roundoff at triangle edges and extrapolation in raster helper lanes.
    weights = saturate(weights);
    weights /= weights.x + weights.y + weights.z;

    float3 offset = 0.0;
    [unroll]
    for (uint i = 0u; i < 3u; ++i)
    {
        const float distanceToPlane = dot(position - positions[i], normals[i]);
        // Only project outward; concave regions must not move into the mesh.
        offset -= weights[i] * min(distanceToPlane, 0.0) * normals[i];
    }
    return offset;
}

#endif // SPARK_LIB_GEOMETRY_SHADING_HLSLI
