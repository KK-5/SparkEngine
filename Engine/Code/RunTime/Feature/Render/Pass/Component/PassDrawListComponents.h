#pragma once

#include <cstdint>

#include <RHI/Context/RHIContext.h>

#include <Drawable/DrawTag.h>

namespace Spark::Render
{
    // Components on a pass that draws a list of scene instances, a DrawBin at a time
    // (DrawBin/DrawBin.h). DrawBinSystem adds and writes all but PassDrawList.

    //! Added by .Draw<Tag>(): the pass draws the instances whose draw mask has this bit, in
    //! the order and culled where Tag declares.
    struct PassDrawList
    {
        uint32_t    m_drawMaskBitIndex = 0;
        DrawOrder   m_order            = DrawOrder::Any;
        DrawCulling m_culling          = DrawCulling::Gpu;
    };

    //! Where a key index's instances go in a view's part of a pass's indirect arguments. HLSL
    //! mirror comes with the binning shader.
    struct IndirectLayoutEntry
    {
        static constexpr uint32_t NotDrawn = UINT32_MAX;

        uint32_t m_countIndex  = NotDrawn;   // which of the view's counts its draw reads: DrawBin::m_index
        uint32_t m_firstRecord = 0;          // its draw's first record: DrawBin::m_firstDraw
    };

    //! How a pass whose list is culled on the GPU lays out its indirect arguments, the same in
    //! each view's part: what the pass's InstanceBinning reads. Written every frame.
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
