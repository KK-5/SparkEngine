// One orthographic ray per pixel along +z. A hit is colored by its barycentrics, a corner per
// vertex (0 red, 1 green, 2 blue), at full brightness for instance 0 and half for instance 1.

RaytracingAccelerationStructure g_Scene  : register(t0, space0);
RWTexture2D<float4>             g_Output : register(u0, space0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    g_Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
    {
        return;
    }

    float2 ndc = (float2(id.xy) + 0.5) / float2(width, height) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    ndc.x *= float(width) / float(height);

    RayDesc ray;
    ray.Origin    = float3(ndc, -1.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin      = 0.0;
    ray.TMax      = 10.0;

    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_Scene, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();

    float3 color = float3(0.1, 0.1, 0.1);
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        const float2 b = query.CommittedTriangleBarycentrics();
        color = float3(1.0 - b.x - b.y, b.x, b.y);
        if (query.CommittedInstanceID() == 1)
        {
            color *= 0.5;
        }
    }
    g_Output[id.xy] = float4(color, 1.0);
}
