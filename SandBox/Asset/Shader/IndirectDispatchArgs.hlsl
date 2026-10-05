// Writes the arguments of an indirect dispatch into a render graph buffer: the thread
// groups that cover a band on the left of the view, whose width swings with time. The pass
// that dispatches by it never learns the size on the CPU.

#include <Shaders/ViewBindings.hlsli>

struct ScopeParameters
{
    float time;
    uint  viewIndex;
};

#include <Shaders/ScopeBindings.hlsli>

// The pattern shader's [numthreads]: an indirect dispatch counts groups, not threads.
static const uint kGroupSize = 8;

// One RHI::DispatchIndirectCommand: groupCountX, groupCountY, groupCountZ.
RWStructuredBuffer<uint> g_Args : register(u0, space2);

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint2 size = uint2(GetView(g_Scope.viewIndex).viewSizeAndInvSize.xy);

    const float share = 0.5 + 0.4 * sin(g_Scope.time);
    const uint  width = uint(float(size.x) * share);

    g_Args[0] = (width + kGroupSize - 1) / kGroupSize;
    g_Args[1] = (size.y + kGroupSize - 1) / kGroupSize;
    g_Args[2] = 1;
}
