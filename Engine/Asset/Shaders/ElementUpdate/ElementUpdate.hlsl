// ElementUpdate.hlsl — writes the frame's new contents of some elements of an array into it.
// g_Updates holds their indices, then their contents packed in the same order (an
// ElementUpdateBuffer). A thread copies one uint: the threads of an element are next to each
// other, so its words are written together.

struct ScopeParameters
{
    uint updateCount;     // elements updated
    uint elementWords;    // uints in an element
    uint indicesOffset;   // bytes into g_Updates where the indices start
    uint elementsOffset;  // bytes into g_Updates where the contents start
};

#include <Shaders/ScopeBindings.hlsli>

ByteAddressBuffer   g_Updates  : register(t0, space2);
RWByteAddressBuffer g_Elements : register(u0, space2);

// Bytes of a uint: an index, and what a thread copies.
static const uint UintSize = 4;

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint update = id.x / g_Scope.elementWords;
    if (update >= g_Scope.updateCount)
    {
        return;
    }
    const uint word = id.x % g_Scope.elementWords;

    const uint index = g_Updates.Load(g_Scope.indicesOffset + update * UintSize);
    const uint value = g_Updates.Load(g_Scope.elementsOffset + (update * g_Scope.elementWords + word) * UintSize);
    g_Elements.Store((index * g_Scope.elementWords + word) * UintSize, value);
}
