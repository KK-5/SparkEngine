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

#include "LightData.hlsli"

StructuredBuffer<LightData> g_Lights : register(t0, space0);

// The active skybox's baked environment. g_PrefilteredCube's mips are a ROUGHNESS axis
// (mip n == roughness n/(mipCount-1)), not a detail chain — always sample it with an
// explicit LOD.
TextureCube  g_IrradianceCube  : register(t1, space0);
TextureCube  g_PrefilteredCube : register(t2, space0);
SamplerState g_IBLSampler      : register(s0, space0);

cbuffer SceneConstants : register(b0, space0)
{
    uint  g_LightCount;               // number of valid entries in g_Lights this frame
    uint  g_IBLPrefilteredMipCount;   // 0 == no environment bound; see HasEnvironmentIBL
    float g_EnvIntensity;             // SkyboxComponent::m_intensity, or 1 with no skybox
    uint  g_ScenePad2;
};

LightData GetLight(uint i)
{
    return g_Lights[i];
}

//! EVERY read of the two cubes must be gated on this: they share one descriptor table with
//! g_Lights, so when lights are bound and the environment is not, their slots hold stale
//! descriptors rather than nothing.
bool HasEnvironmentIBL()
{
    return g_IBLPrefilteredMipCount > 0;
}

//! Mip to sample g_PrefilteredCube at for a given roughness. The ladder is deliberately
//! non-uniform (levels bunch up at low roughness), so this is NOT
//! perceptualRoughness * (mipCount - 1).
//!
//! MUST remain the exact inverse of EnvironmentBaker::LodToRoughness (C++). Those are two
//! independent copies in two languages; if they drift apart nothing fails -- every
//! reflection simply comes out uniformly too sharp or too blurred.
float RoughnessToLod(float perceptualRoughness, uint mipCount)
{
    float t = 1.0 - perceptualRoughness;
    return float(mipCount - 1) * (1.0 - t * t);
}

#endif // SPARK_SCENE_BINDINGS_HLSL
