// InstanceCulling.hlsl — turns g_Instances into the list of draws an indirect call reads.
// One thread per instance slot; a slot whose DrawMask lacks the pass's bit (a hole, geometry
// not ready, or another kind of object) writes nothing. Nothing is culled yet: every
// instance carrying the bit gets a record.
//
// A thread takes the next free place in the list from the counter, so the records are
// packed from the start of the buffer and in no fixed order.
//
// The pass runs this twice: first to zero the counter, then to write.

#include <Shaders/InstanceBindings.hlsli>
#include <Shaders/GeometryBindings.hlsli>
#include <Shaders/IndirectCommands.hlsli>

struct ScopeParameters
{
    uint isClearCount;  // 1: zero the counter and write no record
    uint slotCount;     // slots of g_Instances to go through, holes included
    uint drawMaskBit;   // the classification bit this list is of
};

#include <Shaders/ScopeBindings.hlsli>

RWStructuredBuffer<DrawIndexedIndirectCommand> g_DrawArguments : register(u0, space2);
RWStructuredBuffer<uint>                       g_DrawCount     : register(u1, space2);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (g_Scope.isClearCount != 0)
    {
        // The dispatch is of a whole group: one thread does it.
        if (id.x == 0)
        {
            g_DrawCount[0] = 0;
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
    InterlockedAdd(g_DrawCount[0], 1, place);
    g_DrawArguments[place] = command;
}
