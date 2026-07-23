#pragma once

#include <cstdint>

#include <Math/Matrix4x4.h>

namespace Spark::Render
{
    //! Per-instance GPU record, one element of the global g_Instances
    //! StructuredBuffer (space4). HLSL mirror lives in InstanceData.hlsli — the
    //! two MUST stay byte-for-byte identical (the static_assert below is the only
    //! automatic guard until RHI reflection can read StructuredBuffer element
    //! fields; see TODO_InstanceBindingSystemPlan.md §2 / §G5).
    struct InstanceData
    {
        Math::Matrix4X4 m_model{Math::Matrix4X4Const::IDENTITY};        // object -> world (64B)
        Math::Matrix4X4 m_normalMatrix{Math::Matrix4X4Const::IDENTITY}; // inverse-transpose of model (64B)
        uint32_t        m_materialIndex = 0;   // slot into g_Materials (space3), resolved per frame
        uint32_t        m_pad[3]        = {0, 0, 0};
    };

    // 144B, 16B-aligned. StructuredBuffer elements are tightly C-packed (no cbuffer 16B
    // rounding), so sizeof must match the HLSL
    // `float4x4 Model; float4x4 NormalMatrix; uint MaterialIndex; uint3 _Pad;`.
    static_assert(sizeof(InstanceData) == 144,
        "InstanceData must stay 144 bytes to match InstanceData.hlsli; add padding "
        "deliberately when introducing new fields.");
}
