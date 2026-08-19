#pragma once

#include <cstdint>

#include <Math/Vector4.h>

#include <Material/Components.h>   // MaterialTexSlotCount / MaterialTexSlot

namespace Spark::Render
{
    //! Sentinel for "no texture" in MaterialData index fields. Matches
    //! RHI::ImageView::InvalidBindlessIndex (uint32_t(-1)); the GBuffer PS falls back
    //! to the scalar factor when a material's slot index is this.
    inline constexpr uint32_t InvalidTextureIndex = 0xFFFFFFFFu;

    //! Per-material GPU record, one element of the global g_Materials StructuredBuffer
    //! (space3). HLSL mirror lives in MaterialData.hlsli — the two MUST stay
    //! byte-for-byte identical (the static_assert below is the only automatic guard).
    //!
    //! Assembled each frame by MaterialBindingSystem from Material::MaterialParams.
    //! m_texIndices[slot] is the SM6.6 bindless heap index of the resolved texture for
    //! that MaterialTexSlot (sampled via ResourceDescriptorHeap[i] in the GBuffer PS),
    //! or InvalidTextureIndex when the material has no map in that slot — see
    //! TODO_MaterialSystemPlan.md appendix B.6.
    struct MaterialData
    {
        Math::Vector4 m_baseColor{0.8f, 0.8f, 0.8f, 1.0f};   // rgb (+a reserved)
        float         m_metallic    = 0.0f;
        float         m_roughness   = 0.5f;
        float         m_specular    = 0.5f;                  // dielectric F0 scale
        float         m_normalScale = 1.0f;                  // tangent XY scale (Normal map)
        Math::Vector4 m_emissive{0.0f, 0.0f, 0.0f, 1.0f};    // rgb = emissive factor, a = strength
        // Bindless texture index per MaterialTexSlot; InvalidTextureIndex = no map.
        uint32_t      m_texIndices[Material::MaterialTexSlotCount];

        MaterialData()
        {
            for (uint32_t& idx : m_texIndices)
            {
                idx = InvalidTextureIndex;
            }
        }
    };

    // StructuredBuffer elements are tightly C-packed (no cbuffer 16B rounding), so sizeof
    // must match the HLSL struct in MaterialData.hlsli. 16 (baseColor) + 16 (4 floats) +
    // 16 (emissive) + 4*5 (indices) = 68. If this fires, a slot was added: bump
    // SPARK_MATERIAL_TEX_SLOT_COUNT in MaterialData.hlsli to match MaterialTexSlot::Count.
    static_assert(Material::MaterialTexSlotCount == 5,
        "MaterialData.hlsli hardcodes SPARK_MATERIAL_TEX_SLOT_COUNT=5; keep them in sync.");
    static_assert(sizeof(MaterialData) == 68,
        "MaterialData must stay 68 bytes to match MaterialData.hlsli.");
}
