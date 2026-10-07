#pragma once

#include <cstdint>

namespace Spark::Render
{
    //! Per-geometry GPU record, one element of the global g_Geometries StructuredBuffer
    //! (space6): what an indexed draw of the geometry pool's native buffer needs to draw
    //! one mesh. HLSL mirror lives in GeometryData.hlsli — the two MUST stay byte-for-byte
    //! identical.
    struct GeometryData
    {
        uint32_t m_firstIndex   = 0;
        uint32_t m_indexCount   = 0;
        int32_t  m_vertexOffset = 0;
    };

    static_assert(sizeof(GeometryData) == 12,
        "GeometryData must stay 12 bytes to match GeometryData.hlsli.");
}
