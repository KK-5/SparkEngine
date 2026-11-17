#pragma once

#include <cstddef>
#include <cstdint>

#include <Math/Matrix4x4.h>

namespace Spark::Render
{
    //! Per-instance GPU record, one element of the global g_Instances
    //! StructuredBuffer (space4). HLSL mirror lives in InstanceData.hlsli — the
    //! two MUST stay byte-for-byte identical (the static_assert below is the only
    //! automatic guard until RHI reflection can read StructuredBuffer element
    //! fields).
    struct InstanceData
    {
        Math::Matrix4X4 m_model{Math::Matrix4X4Const::IDENTITY};        // object -> world (64B)
        Math::Matrix4X4 m_prevModel{Math::Matrix4X4Const::IDENTITY};    // last frame's m_model (64B)
        Math::Matrix4X4 m_normalMatrix{Math::Matrix4X4Const::IDENTITY}; // inverse-transpose of model (64B)
        uint32_t        m_materialIndex = 0;   // slot into g_Materials (space3), resolved per frame
        uint32_t        m_drawMask      = 0;   // DrawMask.h; 0 is a record nobody draws
        uint32_t        m_geometryIndex = 0;   // slot into g_Geometries (space6); only meaningful while m_drawMask is set
        uint32_t        m_pipelineKeyIndex = 0;   // PipelineKeyIndex.h; what each pass's indirect layout table is addressed by
        uint32_t        m_motionHistoryValid = 0; // the previous corresponding surface exists
        uint32_t        m_padding0 = 0;
        uint32_t        m_padding1 = 0;
        uint32_t        m_padding2 = 0;
    };

    // 224B, padded to a multiple of 16. StructuredBuffer elements are tightly
    // C-packed (no cbuffer rounding), so fields and stride must match InstanceData.hlsli.
    static_assert(sizeof(InstanceData) == 224,
        "InstanceData must stay 224 bytes to match InstanceData.hlsli; add padding "
        "deliberately when introducing new fields.");
    static_assert(offsetof(InstanceData, m_motionHistoryValid) == 208,
        "Instance motion history validity must match InstanceData.hlsli.");
}
