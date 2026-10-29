// An acceleration structure (t1) beside a texture (t0) in one space.

RaytracingAccelerationStructure g_Scene : register(t1, space2);
Texture2D<float4>               g_Input : register(t0, space2);
RWTexture2D<float4>             g_Output : register(u0, space2);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    RayDesc ray;
    ray.Origin    = float3(id.xy, 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin      = 0.0;
    ray.TMax      = 1000.0;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(g_Scene, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();

    const bool hit = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    g_Output[id.xy] = hit ? g_Input[id.xy] : float4(0.0, 0.0, 0.0, 1.0);
}
