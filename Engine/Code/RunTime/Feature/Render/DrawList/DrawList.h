#pragma once

#include <cstdint>

#include <PipelineState/PipelineKey.h>
#include <PipelineState/PipelineKeyIndex.h>

namespace Spark::Render
{
    // What is known of each draw list, the instances a tag classifies, as elements of the
    // DrawContext (DrawContext.h). The encoding writes them; DrawGroupSystem makes the groups
    // (DrawGroup.h) from them.

    //! One pipeline key among the instances of a draw list: how many encoded instances carry
    //! it. The encoding publishes one a key the list has (DrawListKeyCounter), which is what
    //! the CPU knows of a list the GPU culls.
    struct DrawListKey
    {
        uint32_t         m_drawMaskBitIndex = 0;
        PipelineKeyIndex m_keyIndex;
        PipelineKey      m_key;
        uint32_t         m_instanceCount = 0;
    };
}
