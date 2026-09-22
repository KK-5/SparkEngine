// Per-scene shader inputs (the "SceneBindings" group), reserved at register space0 —
// the lowest-frequency, most-global tier (one value per frame, shared across all views
// and passes). Shared engine header — any shader that needs scene lights does:
//     #include <Shaders/SceneBindings.hlsli>
// and the engine fills g_Lights / g_LightCount via SceneBindingSystem.
//
// Binding-space convention (by update frequency, low = stable): space0 per-scene,
// space1 per-view, space2 per-pass, space3 per-material, space4 per-object.
#ifndef SPARK_SCENE_BINDINGS_HLSL
#define SPARK_SCENE_BINDINGS_HLSL

#include "LightData.hlsli"
#include <Shaders/Lib/Shadow/ShadowViewData.hlsli>

StructuredBuffer<LightData> g_Lights : register(t0, space0);

// Indexed by LightData::shadowIndex plus a cube face — entries for unallocated
// tiles are zeroed, so never read one without checking shadowIndex >= 0 first.
StructuredBuffer<ShadowViewData> g_ShadowViews : register(t4, space0);

// The active skybox's baked environment. g_PrefilteredCube's mips are a ROUGHNESS axis
// (mip n == roughness n/(mipCount-1)), not a detail chain — always sample it with an
// explicit LOD.
TextureCube  g_IrradianceCube  : register(t1, space0);
TextureCube  g_PrefilteredCube : register(t2, space0);

// Pre-integrated split-sum BRDF (DFG) table, baked offline by SandBox BRDFLutGen.
// uv = (NoV, perceptualRoughness), .rg = (scale, bias) applied to F0. Scene-independent,
// but it shares this group because it is bound once and read by every shading path.
Texture2D    g_BRDFLut         : register(t3, space0);

SamplerState g_IBLSampler      : register(s0, space0);

cbuffer SceneConstants : register(b0, space0)
{
    uint  g_LightCount;               // number of valid entries in g_Lights this frame
    uint  g_IBLPrefilteredMipCount;   // 0 == no environment bound; see HasEnvironmentIBL
    float g_EnvIntensity;             // SkyboxComponent::m_intensity, or 1 with no skybox
    float g_ShadowAtlasTexelSize;     // 1 / atlas resolution; the PCF step in atlas UV

    // Engine clock (Core FrameTime), also mirrored into ViewBindings for shaders that bind
    // only the per-view group. Both copies come from the same source and always agree; the
    // g_Scene prefix is only here because cbuffer members share one global namespace, so a
    // shader including both headers would otherwise see a redefinition.
    uint  g_SceneFrameNumber;
    float g_SceneGameTime;            // seconds; stops under pause, stretches under time scale
    float g_ScenePrevGameTime;
    float g_SceneDeltaTime;           // game time step
};

LightData GetLight(uint i)
{
    return g_Lights[i];
}

ShadowViewData GetShadowView(int i)
{
    return g_ShadowViews[i];
}

//! EVERY read of the two cubes must be gated on this: they share one descriptor table with
//! g_Lights, so when lights are bound and the environment is not, their slots hold stale
//! descriptors rather than nothing.
bool HasEnvironmentIBL()
{
    return g_IBLPrefilteredMipCount > 0;
}

//! WORKAROUND, not shading. A pass's space0 table offsets come from its OWN reflection,
//! while the group writes its descriptors in the GROUP's order, so a shader referencing only
//! part of space0 shifts every slot past the gap -- g_IrradianceCube starts reading g_Lights.
//! Every shader binding this group adds the result to its output so its reflection is
//! complete. Delete this and its call sites together with the real fix: a group-owned space's
//! layout must come from the group. See TODO_StructureAlignPlan.md.
float SpaceZeroKeepAlive()
{
    float keep = GetShadowView(0).uvMinMax.x + (float)g_Lights[0].type;
    if (HasEnvironmentIBL())
    {
        keep += g_IrradianceCube.SampleLevel(g_IBLSampler, float3(0, 1, 0), 0).r
              + g_PrefilteredCube.SampleLevel(g_IBLSampler, float3(0, 1, 0), 0).r
              + g_BRDFLut.SampleLevel(g_IBLSampler, float2(1, 0), 0).r;
    }
    return keep * 1e-30;
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
