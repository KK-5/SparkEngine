// Shared opaque-triangle queries. No material evaluation, camera or integrator policy.
#ifndef SPARK_LIB_RAY_TRACING_HLSLI
#define SPARK_LIB_RAY_TRACING_HLSLI

#include <Shaders/RayTracingSceneBindings.hlsli>

struct RayTracingHit
{
    uint InstanceIndex;     // CommittedInstanceID, NOT CommittedInstanceIndex: g_Instances slot
    uint GeometryIndex;     // BLAS-local; engine mesh BLASes currently contain one geometry
    uint PrimitiveIndex;
    float2 Barycentrics;    // weights of vertices 1 and 2; vertex 0 is 1 - x - y
    float T;               // world distance only when the input ray direction is normalized
    bool FrontFace;
};

// Does not accept the first encountered hit: shading needs the closest one. FORCE_OPAQUE
// is deliberate; alpha-tested/translucent surfaces need a separate candidate policy.
bool TraceClosestOpaqueTriangle(RayDesc ray, uint instanceMask, out RayTracingHit hit)
{
    hit = (RayTracingHit)0;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(g_RayTracingScene, RAY_FLAG_NONE, instanceMask, ray);
    while (query.Proceed())
    {
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        return false;
    }
    hit.InstanceIndex = query.CommittedInstanceID();
    hit.GeometryIndex = query.CommittedGeometryIndex();
    hit.PrimitiveIndex = query.CommittedPrimitiveIndex();
    hit.Barycentrics = query.CommittedTriangleBarycentrics();
    hit.T = query.CommittedRayT();
    hit.FrontFace = query.CommittedTriangleFrontFace();
    return true;
}

// True when the entire caller-specified [TMin, TMax] segment is unoccluded. The caller
// owns origin bias and endpoint exclusion; visibility can terminate at any opaque hit.
bool TraceOpaqueVisibility(RayDesc ray, uint instanceMask)
{
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(g_RayTracingScene, RAY_FLAG_NONE, instanceMask, ray);
    query.Proceed();
    return query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
}

#endif // SPARK_LIB_RAY_TRACING_HLSLI
