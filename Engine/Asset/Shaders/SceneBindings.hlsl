// Per-scene shader inputs (the "SceneBindings" group), reserved at register space0 —
// the lowest-frequency, most-global tier (one value per frame, shared across all views
// and passes). Shared engine header — any shader that needs scene lights does:
//     #include <Shaders/SceneBindings.hlsl>
// and the engine fills g_Lights / g_LightCount via SceneBindingSystem.
//
// Binding-space convention (by update frequency, low = stable): space0 per-scene,
// space1 per-view, space2 per-pass, space3 per-material, space4 per-object.
#ifndef SPARK_SCENE_BINDINGS_HLSL
#define SPARK_SCENE_BINDINGS_HLSL

// Mirror of Render::LightData (SceneBind/LightData.h) — 64 bytes, keep in sync.
struct LightData
{
    float3 direction; float intensity;   // dir/spot direction (world), radiant intensity
    float3 color;     uint  type;        // rgb; 0=directional, 1=point, 2=spot
    float3 position;  float invRange;    // point/spot world position, 1/range (0=dir)
    float  cosInner;  float cosOuter; float pad0; float pad1;   // spot cone
};

StructuredBuffer<LightData> g_Lights : register(t0, space0);

cbuffer SceneConstants : register(b0, space0)
{
    uint g_LightCount;   // number of valid entries in g_Lights this frame
    uint g_ScenePad0;
    uint g_ScenePad1;
    uint g_ScenePad2;
};

LightData GetLight(uint i)
{
    return g_Lights[i];
}

#endif // SPARK_SCENE_BINDINGS_HLSL
