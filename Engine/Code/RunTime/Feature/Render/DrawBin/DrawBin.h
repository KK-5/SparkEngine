#pragma once

#include <cstdint>

#include <RHI/Context/RHIContext.h>

#include <Pass/Pass.h>
#include <PipelineState/PipelineKey.h>
#include <PipelineState/PipelineKeyIndex.h>
#include <PipelineState/PipelineVariant.h>

namespace Spark::Render
{
    // The elements of the DrawContext (DrawContext.h); what goes on the passes is in
    // PassDrawListComponents.h.

    //! One pipeline key among the instances of a draw list, the instances a tag classifies:
    //! how many encoded instances carry it. The encoding publishes one a key the list has
    //! (DrawListKeyCounter), which is what the CPU knows of a list the GPU culls.
    struct DrawListKey
    {
        uint32_t         m_drawMaskBitIndex = 0;
        PipelineKeyIndex m_keyIndex;
        PipelineKey      m_key;
        uint32_t         m_instanceCount = 0;
    };

    //! One indirect draw of a pass under a view: a DrawGroup of the pass. Its PSO is the pass's
    //! description with its variant, which the render graph builds.
    struct DrawBin
    {
        Pass           m_pass  = NullPass;
        RHI::RHIHandle m_view  = RHI::NullHandle;
        //! Its place among the pass's under the view, which is also which of the view's counts is its.
        uint32_t       m_index = 0;

        PipelineVariant m_variant;

        //! From the start of the view's part of the argument buffer, or of the buffer when it
        //! has no part per view.
        uint32_t m_firstDraw = 0;
        uint32_t m_maxDraws  = 0;
    };
}
