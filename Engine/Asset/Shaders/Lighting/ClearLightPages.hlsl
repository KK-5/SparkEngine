struct ScopeParameters
{
    uint firstWord;
    uint wordCount;
};

#include <Shaders/ScopeBindings.hlsli>

RWByteAddressBuffer g_Elements : register(u0, space2);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x < g_Scope.wordCount)
    {
        g_Elements.Store((g_Scope.firstWord + id.x) * 4u, 0u);
    }
}
