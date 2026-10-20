#pragma once

#include <cstdint>

#include <RHI/Context/RHIContext.h>

#include <Drawable/DrawTag.h>

namespace Spark::Render
{
    // Components on a pass that draws a list of scene instances, a DrawBin at a time
    // (DrawBin/DrawBin.h). DrawBinSystem adds all but PassDrawList and writes them.

    //! Added by .Draw<Tag>(): the pass draws the instances whose draw mask has this bit, in
    //! the order and culled where Tag declares.
    struct PassDrawList
    {
        uint32_t    m_drawMaskBitIndex = 0;
        DrawOrder   m_order            = DrawOrder::Any;
        DrawCulling m_culling          = DrawCulling::Gpu;
    };

    //! Written every frame. Both rounded up a step at a time, so what is sized by them is not
    //! resized every frame.
    struct PassBinLayout
    {
        uint32_t m_binCount   = 0;   // counts per view
        uint32_t m_viewStride = 0;   // records per view
    };

    //! One entry of a pass's bin table. HLSL mirror comes with the binning shader.
    struct DrawBinTableEntry
    {
        static constexpr uint32_t NotDrawn = UINT32_MAX;

        uint32_t m_index = NotDrawn;   // DrawBin::m_index of the key's bin
        uint32_t m_first = 0;          // and its m_firstDraw
    };

    //! The pass's table from a key index to its bin, a host per-frame buffer written whole
    //! every frame. m_entryCount entries are this frame's, of m_capacity it holds.
    struct PassBinTable
    {
        RHI::RHIHandle m_buffer     = RHI::NullHandle;
        uint32_t       m_capacity   = 0;
        uint32_t       m_entryCount = 0;
    };
}
