// InstanceBinning.hlsl — sorts the instances a view sees into the bins of one drawing pass,
// written once for each view. One thread per entry of the view's part of the visible list
// (InstanceCulling.hlsl); a thread past the view's count writes nothing.
//
// The pass's indirect layout sends an instance's pipeline key index to its bin: which of the
// view's counts is the bin's, and where the bin's records start in the view's part of the
// argument buffer. A thread takes the next free place among its bin's records from the bin's count,
// so a bin is packed from its start and in no fixed order. The CPU sized each bin for all
// the instances of its keys, so it does not overflow.
//
// The pass runs this once to zero the counts, then once per view to write its part.

#include <Shaders/InstanceBindings.hlsli>
#include <Shaders/GeometryBindings.hlsli>
#include <Shaders/IndirectCommands.hlsli>

// Mirror of IndirectLayoutEntry (PassIndirectLayout.h).
struct IndirectLayoutEntry
{
    uint CountIndex;     // which of a view's counts is the bin's; NotDrawn when the key is drawn in no bin
    uint FirstRecord;    // the bin's first record in a view's part of the arguments
};

static const uint NotDrawn = 0xFFFFFFFF;

struct ScopeParameters
{
    uint isClearCount;           // 1: zero the counts and write no record
    uint countCapacity;          // counts g_DrawCount holds, for every view
    uint indirectLayoutLength;   // entries g_IndirectLayout holds
    uint entryCount;             // threads that may have an entry: the view's part holds fewer
    uint viewFirstEntry;         // where this view's part starts in g_VisibleInstances
    uint viewOrdinal;            // which count in g_VisibleCount is this view's
    uint viewFirstCount;         // where this view's counts start in g_DrawCount
    uint viewFirstRecord;        // where this view's part starts in g_DrawArguments
};

#include <Shaders/ScopeBindings.hlsli>

StructuredBuffer<uint>              g_VisibleInstances : register(t0, space2);
StructuredBuffer<uint>              g_VisibleCount     : register(t1, space2);
StructuredBuffer<IndirectLayoutEntry> g_IndirectLayout : register(t2, space2);
StructuredBuffer<uint>              g_InstanceIDs      : register(t3, space2);   // position -> g_Instances slot

RWStructuredBuffer<DrawIndexedIndirectCommand> g_DrawArguments : register(u0, space2);
RWStructuredBuffer<uint>                       g_DrawCount     : register(u1, space2);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (g_Scope.isClearCount != 0)
    {
        if (id.x < g_Scope.countCapacity)
        {
            g_DrawCount[id.x] = 0;
        }
        return;
    }

    if (id.x >= g_Scope.entryCount || id.x >= g_VisibleCount[g_Scope.viewOrdinal])
    {
        return;
    }

    const uint         position = g_VisibleInstances[g_Scope.viewFirstEntry + id.x];
    const InstanceData instance = g_Instances[g_InstanceIDs[position]];
    // Past the table a read gives 0, which is bin 0's entry, not "not drawn".
    if (instance.PipelineKeyIndex >= g_Scope.indirectLayoutLength)
    {
        return;
    }
    const IndirectLayoutEntry bin = g_IndirectLayout[instance.PipelineKeyIndex];
    if (bin.CountIndex == NotDrawn)
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
    // the slot at this position as its INSTANCE_INDEX.
    command.FirstInstance = position;

    uint place;
    InterlockedAdd(g_DrawCount[g_Scope.viewFirstCount + bin.CountIndex], 1, place);
    g_DrawArguments[g_Scope.viewFirstRecord + bin.FirstRecord + place] = command;
}
