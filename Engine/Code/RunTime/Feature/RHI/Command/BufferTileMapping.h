#pragma once

#include <RHI/Resource/Buffer/Buffer.h>

namespace Spark::RHI
{
    //! Maps a page range of a reserved buffer onto the memory of a backing buffer, or unmaps it.
    //! Offsets and sizes are in bytes, each a whole number of DeviceLimits::m_reservedPageSizeInBytes.
    struct BufferTileMapping
    {
        const Buffer* m_buffer = nullptr;
        uint64_t      m_byteOffset = 0;
        uint64_t      m_byteCount = 0;

        //! Null unmaps the range. Else a buffer of BufferBindFlags::TileBacking and, the flag aside,
        //! the reserved buffer's bind flags.
        const Buffer* m_backingBuffer = nullptr;
        uint64_t      m_backingByteOffset = 0;
    };
}
