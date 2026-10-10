// InstanceCulling.hlsl — turns g_Instances into the draws indirect calls read, written once
// for each view. One thread per instance slot; a slot whose DrawMask lacks the pass's bit (a
// hole, geometry not ready, or another kind of object) writes nothing, and neither does an
// instance whose box is outside the view's frustum.
//
// Each view has a stretch of the argument buffer and a count of its own. A thread takes the
// next free place among its view's draws from the count, so they are packed from the start
// of the stretch and in no fixed order.
//
// The pass runs this once to zero the counts, then once per view to write its draws.

#include <Shaders/ViewBindings.hlsli>
#include <Shaders/InstanceBindings.hlsli>
#include <Shaders/GeometryBindings.hlsli>
#include <Shaders/IndirectCommands.hlsli>

struct ScopeParameters
{
    uint isClearCount;  // 1: zero the counts and write no draw
    uint slotCount;     // slots of g_Instances to go through, holes included
    uint drawMaskBit;   // the classification bit the draws are of
    uint viewCapacity;  // views g_DrawCount has a count for
    uint firstDraw;     // where this view's draws start in g_DrawArguments
    uint viewOrdinal;   // which count in g_DrawCount is this view's
    uint viewIndex;     // the view's row of g_Views
};

#include <Shaders/ScopeBindings.hlsli>

RWStructuredBuffer<DrawIndexedIndirectCommand> g_DrawArguments : register(u0, space2);
RWStructuredBuffer<uint>                       g_DrawCount     : register(u1, space2);

// True when the box lies wholly outside the plane. A point is inside when dot(plane, point) >= 0.
bool IsBoxOutside(float4 plane, float3 center, float3 extents)
{
    return dot(plane.xyz, center) + dot(abs(plane.xyz), extents) + plane.w < 0.0;
}

// The box is in the instance's own space and so are the planes, which are rows of
// localToClip put together. Clip z runs from w at the near plane to 0 at the far one.
//
// The near plane is left out for every view. A shadow view draws with depth clipping off so
// that what stands before its near plane still casts, and for a perspective view the four
// sides, which meet at the eye, leave the near plane next to nothing to reject.
bool IsBoxOutsideFrustum(float4x4 localToClip, float3 center, float3 extents)
{
    return IsBoxOutside(localToClip[3] + localToClip[0], center, extents)
        || IsBoxOutside(localToClip[3] - localToClip[0], center, extents)
        || IsBoxOutside(localToClip[3] + localToClip[1], center, extents)
        || IsBoxOutside(localToClip[3] - localToClip[1], center, extents)
        || IsBoxOutside(localToClip[2], center, extents);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (g_Scope.isClearCount != 0)
    {
        if (id.x < g_Scope.viewCapacity)
        {
            g_DrawCount[id.x] = 0;
        }
        return;
    }

    if (id.x >= g_Scope.slotCount)
    {
        return;
    }

    const InstanceData instance = g_Instances[id.x];
    if ((instance.DrawMask & g_Scope.drawMaskBit) == 0)
    {
        return;
    }

    const GeometryData geometry = g_Geometries[instance.GeometryIndex];

    // Unjittered: what is visible does not move with the subpixel offset.
    const float4x4 localToClip = mul(GetView(g_Scope.viewIndex).viewProjectionNoAA, instance.Model);
    if (IsBoxOutsideFrustum(localToClip, geometry.BoundsCenter, geometry.BoundsExtents))
    {
        return;
    }

    DrawIndexedIndirectCommand command;
    command.IndexCount    = geometry.IndexCount;
    command.InstanceCount = 1;
    command.FirstIndex    = geometry.FirstIndex;
    command.VertexOffset  = geometry.VertexOffset;
    // The per-instance ID stream is read from here on, so the draw's vertex shader gets
    // this slot as its INSTANCE_INDEX.
    command.FirstInstance = id.x;

    uint place;
    InterlockedAdd(g_DrawCount[g_Scope.viewOrdinal], 1, place);
    g_DrawArguments[g_Scope.firstDraw + place] = command;
}
