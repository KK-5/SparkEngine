#pragma once

#include <Math/Vector4.h>

namespace Spark::Render
{
    //! Per-material GPU record, one element of the global g_Materials StructuredBuffer
    //! (space2). HLSL mirror lives in MaterialData.hlsli — the two MUST stay
    //! byte-for-byte identical (the static_assert below is the only automatic guard).
    //!
    //! Assembled each frame by MaterialBindingSystem from Material::MaterialParams.
    //! Texture addressing (a texIndex field, resolved from the material's texture
    //! RHIHandle) belongs to the texture phase — see TODO_MaterialSystemPlan.md
    //! appendix A — and is intentionally absent here.
    struct MaterialData
    {
        Math::Vector4 m_baseColor{0.8f, 0.8f, 0.8f, 1.0f};   // rgb (+a reserved)
        float         m_metallic  = 0.0f;
        float         m_roughness = 0.5f;
        float         m_specular  = 0.5f;                    // dielectric F0 scale
        float         m_pad       = 0.0f;                    // -> 32B, 16B aligned
    };

    // 32B. StructuredBuffer elements are tightly C-packed (no cbuffer 16B rounding),
    // so sizeof must match the HLSL struct in MaterialData.hlsli; add padding
    // deliberately when introducing new fields.
    static_assert(sizeof(MaterialData) == 32,
        "MaterialData must stay 32 bytes to match MaterialData.hlsli.");
}
