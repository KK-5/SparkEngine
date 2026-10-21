#pragma once

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <Drawable/DrawMask.h>
#include <PipelineState/PipelineKey.h>

#include "DrawContext.h"

namespace Spark::Render
{
    //! The encoding's count of its instances by draw-mask bit and pipeline key. Counted here,
    //! where a key's index addresses an array, and published once at the end: looking an element
    //! up by value for every instance would cost too much.
    class DrawListKeyCounter
    {
    public:
        DrawListKeyCounter();

        //! An encoded instance, under each bit of drawMask. 0 counts nowhere: no list has it.
        void Count(PipelineKey key, DrawMask drawMask);

        //! A DrawListKey for each bit and key counted at least once, then counts from 0 again.
        void Publish(DrawContext& drawCtx);

    private:
        struct KeyCounts
        {
            PipelineKey                              m_key;
            eastl::array<uint32_t, DrawMaskBitCount> m_counts{};
        };

        //! By key index, as many as keys have one.
        eastl::vector<KeyCounts> m_keyCounts;
    };
}
