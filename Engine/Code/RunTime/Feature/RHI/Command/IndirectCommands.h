#pragma once

#include <cstdint>

namespace Spark::RHI
{
    //! The records an indirect call reads from its argument buffer. Field for field what
    //! both APIs define (D3D12_DRAW_ARGUMENTS / VkDrawIndirectCommand and their siblings),
    //! so a shader or the CPU writes one layout for every backend. Records are packed:
    //! consecutive ones are sizeof(record) apart.

    struct DrawIndirectCommand
    {
        uint32_t m_vertexCount   = 0;
        uint32_t m_instanceCount = 0;
        uint32_t m_firstVertex   = 0;
        uint32_t m_firstInstance = 0;
    };

    struct DrawIndexedIndirectCommand
    {
        uint32_t m_indexCount    = 0;
        uint32_t m_instanceCount = 0;
        uint32_t m_firstIndex    = 0;
        int32_t  m_vertexOffset  = 0;
        uint32_t m_firstInstance = 0;
    };

    struct DispatchIndirectCommand
    {
        uint32_t m_groupCountX = 0;
        uint32_t m_groupCountY = 0;
        uint32_t m_groupCountZ = 0;
    };

    static_assert(sizeof(DrawIndirectCommand) == 16, "Must match the native non-indexed draw record.");
    static_assert(sizeof(DrawIndexedIndirectCommand) == 20, "Must match the native indexed draw record.");
    static_assert(sizeof(DispatchIndirectCommand) == 12, "Must match the native dispatch record.");
}
