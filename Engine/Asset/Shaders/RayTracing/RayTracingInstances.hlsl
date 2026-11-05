#include <Shaders/InstanceBindings.hlsli>
#include <Shaders/GeometryBindings.hlsli>

struct ScopeParameters
{
    uint instanceCount;
};

#include <Shaders/ScopeBindings.hlsli>

// Matches RHI::RayTracingInstance, including its three row-major transform rows.
struct RayTracingInstance
{
    float4 Transform0;
    float4 Transform1;
    float4 Transform2;
    uint InstanceIdAndMask;       // Low 24 bits: instance ID; high 8 bits: mask.
    uint HitGroupOffsetAndFlags;  // Low 24 bits: hit group offset; high 8 bits: flags.
    uint2 AccelerationStructureAddress;
};

StructuredBuffer<uint> g_InstanceIDs : register(t0, space2);
RWStructuredBuffer<RayTracingInstance> g_RayTracingInstances : register(u0, space2);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_Scope.instanceCount)
    {
        return;
    }

    const uint slot = g_InstanceIDs[id.x];
    const InstanceData instance = g_Instances[slot];
    RayTracingInstance result = (RayTracingInstance)0;
    result.Transform0 = instance.Model[0];
    result.Transform1 = instance.Model[1];
    result.Transform2 = instance.Model[2];
    result.InstanceIdAndMask = slot;

    if (instance.DrawMask != 0)
    {
        const uint2 address = g_Geometries[instance.GeometryIndex].BottomLevelAddress;
        if (any(address != 0))
        {
            result.InstanceIdAndMask |= 0xFF000000u;
            result.AccelerationStructureAddress = address;
        }
    }

    g_RayTracingInstances[id.x] = result;
}
