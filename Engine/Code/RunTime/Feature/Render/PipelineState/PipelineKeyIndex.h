#pragma once

#include <cstdint>

#include "PipelineKey.h"

namespace Spark::Render
{
    //! The number a pipeline key goes by on the GPU, where a binning thread has only the
    //! instance's record: a table indexed by it is as long as the keys in use, not the key
    //! space. The same while any instance carries the key.
    struct PipelineKeyIndex
    {
        uint32_t m_value = 0;

        bool operator==(const PipelineKeyIndex& rhs) const { return m_value == rhs.m_value; }
        bool operator!=(const PipelineKeyIndex& rhs) const { return m_value != rhs.m_value; }
    };

    //! For now the key's own value. Its one caller is meant to be the encoding, which then
    //! keeps the indices of the keys in use itself.
    inline PipelineKeyIndex PipelineKeyIndexOf(PipelineKey key)
    {
        return PipelineKeyIndex{ key.m_value };
    }
}
