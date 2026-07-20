#pragma once

#include <cstdint>

#include <Math/Vector4.h>

namespace Spark::Render
{
    //! Sentinel for "no texture" in MaterialData index fields. Matches
    //! RHI::ImageView::InvalidBindlessIndex (uint32_t(-1)); the GBuffer PS falls back
    //! to the scalar base color when a material's index is this.
    inline constexpr uint32_t InvalidTextureIndex = 0xFFFFFFFFu;

    //! Per-material GPU record, one element of the global g_Materials StructuredBuffer
    //! (space3). HLSL mirror lives in MaterialData.hlsli — the two MUST stay
    //! byte-for-byte identical (the static_assert below is the only automatic guard).
    //!
    //! Assembled each frame by MaterialBindingSystem from Material::MaterialParams.
    //! m_baseColorTexIndex is the SM6.6 bindless heap index of the resolved base-color
    //! texture (sampled via ResourceDescriptorHeap[i] in the GBuffer PS), or
    //! InvalidTextureIndex when the material has no base-color map — see
    //! TODO_MaterialSystemPlan.md appendix B.6.
    struct MaterialData
    {
        Math::Vector4 m_baseColor{0.8f, 0.8f, 0.8f, 1.0f};       // rgb (+a reserved)
        float         m_metallic          = 0.0f;
        float         m_roughness         = 0.5f;
        float         m_specular          = 0.5f;                // dielectric F0 scale
        uint32_t      m_baseColorTexIndex = InvalidTextureIndex; // bindless idx, -> 32B
    };

    // 32B. StructuredBuffer elements are tightly C-packed (no cbuffer 16B rounding),
    // so sizeof must match the HLSL struct in MaterialData.hlsli; add padding
    // deliberately when introducing new fields.
    static_assert(sizeof(MaterialData) == 32,
        "MaterialData must stay 32 bytes to match MaterialData.hlsli.");
}
