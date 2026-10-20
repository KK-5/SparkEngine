#pragma once

#include <cstdint>

#include <RHI/Context/RHIContext.h>
#include <RHI/Pipeline/PipelineState.h>

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

    //! One indirect draw of a pass: a DrawGroup with its PSO. Under a view, or under every view
    //! the pass renders (NullHandle) when its list is culled on the GPU, whose groups are the
    //! same for every view and lie at the start of each view's part of the arguments.
    struct DrawBin
    {
        Pass           m_pass  = NullPass;
        RHI::RHIHandle m_view  = RHI::NullHandle;
        //! Its place among the pass's, which is also which of a view's counts is its.
        uint32_t       m_index = 0;

        PipelineVariant           m_variant;
        //! DrawBinSystem::AcquirePipelines fills it; its DrawPipelineCache owns it.
        const RHI::PipelineState* m_pso = nullptr;

        uint32_t m_firstDraw = 0;
        uint32_t m_maxDraws  = 0;
    };
}
