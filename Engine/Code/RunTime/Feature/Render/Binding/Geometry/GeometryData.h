#pragma once

#include <cstdint>

#include <Math/Vector3.h>

namespace Spark::Render
{
    //! Per-geometry GPU record, one element of the global g_Geometries StructuredBuffer
    //! (space6): what an indexed draw of the geometry pool's native buffer needs to draw
    //! one mesh, and the box culling tests. HLSL mirror lives in GeometryData.hlsli — the
    //! two MUST stay byte-for-byte identical.
    //!
    //! A float3 is followed by a 4-byte field each time, so the record is laid out the same
    //! packed tightly or by std430.
    struct GeometryData
    {
        Math::Vector3 m_boundsCenter  {0.0f};
        uint32_t      m_firstIndex    = 0;
        Math::Vector3 m_boundsExtents {0.0f};
        uint32_t      m_indexCount    = 0;
        int32_t       m_vertexOffset  = 0;
        uint32_t      m_padding[3]    = {};
    };

    static_assert(sizeof(GeometryData) == 48,
        "GeometryData must stay 48 bytes to match GeometryData.hlsli.");
}
