#pragma once

#include <cstdint>

#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    // How a pass whose list is culled on the GPU lays out its indirect arguments. DrawBinSystem
    // writes it every frame; the pass's InstanceBinning reads it.

    //! Where a key index's instances go in a view's part of a pass's indirect arguments. Mirror
    //! in InstanceBinning.hlsl.
    struct IndirectLayoutEntry
    {
        static constexpr uint32_t NotDrawn = UINT32_MAX;

        uint32_t m_countIndex  = NotDrawn;   // which of the view's counts its draw reads: DrawGroup::m_index
        uint32_t m_firstRecord = 0;          // its draw's first record: DrawGroup::m_firstDraw
    };

    //! On the pass: the same layout in each view's part.
    struct PassIndirectLayout
    {
        //! IndirectLayoutEntry by key index, a host per-frame buffer written whole every frame.
        RHI::RHIHandle m_table       = RHI::NullHandle;
        //! This frame's entries: the largest key index of the list, plus one.
        uint32_t       m_tableLength = 0;

        // Both rounded up a step at a time, so what they size is not resized every frame.
        uint32_t m_countsPerView  = 0;
        uint32_t m_recordsPerView = 0;
    };
}
