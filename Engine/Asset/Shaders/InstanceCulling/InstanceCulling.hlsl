// InstanceCulling.hlsl — turns g_Instances into the draws indirect calls read, written once
// for each view. One thread per instance slot; a slot whose DrawMask lacks the pass's bit (a
// hole, geometry not ready, or another kind of object) writes nothing. Nothing is culled
// yet: every instance carrying the bit gets a draw in every view.
//
// Each view has a stretch of the argument buffer and a count of its own. A thread takes the
// next free place among its view's draws from the count, so they are packed from the start
// of the stretch and in no fixed order.
//
// The pass runs this once to zero the counts, then once per view to write its draws.

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
};

#include <Shaders/ScopeBindings.hlsli>

RWStructuredBuffer<DrawIndexedIndirectCommand> g_DrawArguments : register(u0, space2);
RWStructuredBuffer<uint>                       g_DrawCount     : register(u1, space2);

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
