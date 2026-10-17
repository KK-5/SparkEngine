#pragma once

#include <cstdint>

#include <EASTL/array.h>

#include <RHI/Context/RHIContext.h>

#include <PipelineState/PipelineKey.h>

namespace Spark::Render
{
    // Components on a pass that draws a list of scene instances, a DrawBin at a time
    // (DrawBin/DrawBin.h). DrawBinSystem adds all but PassDrawList and writes them.

    //! Added by .Draw<Tag>(): the pass draws the instances whose draw mask has this bit.
    struct PassDrawList
    {
        uint32_t m_drawMaskBitIndex = 0;
    };

    //! Once every key has an assignment for the pass.
    struct PassBinned
    {
    };

    //! Written every frame. Both rounded up a step at a time, so what is sized by them is not
    //! resized every frame.
    struct PassBinLayout
    {
        uint32_t m_binCount   = 0;   // counts per view
        uint32_t m_viewStride = 0;   // records per view
    };

    //! One entry of a pass's key-to-bin table. HLSL mirror comes with the binning shader.
    struct DrawBinTableEntry
    {
        static constexpr uint32_t NotDrawn = UINT32_MAX;

        uint32_t m_index = NotDrawn;   // DrawBinRange::m_index of the key's bin
        uint32_t m_first = 0;          // and its m_first
    };

    //! The pass's key-to-bin table, addressed by PipelineKey::m_value. The CPU mirror is what
    //! is uploaded into buffer, a host per-frame buffer, every frame.
    struct PassBinTable
    {
        eastl::array<DrawBinTableEntry, PipelineKeyCount> m_entries{};
        RHI::RHIHandle                                    m_buffer = RHI::NullHandle;
    };
}
