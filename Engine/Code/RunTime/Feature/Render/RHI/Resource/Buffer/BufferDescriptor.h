#pragma once

#include <HardwareQueue.h>
#include "BufferBindFlags.h"

namespace Spark::RHI
{
    struct BufferDescriptor
    {
        BufferDescriptor() = default;
        BufferDescriptor(BufferBindFlags bindFlags, size_t byteCount);
        virtual ~BufferDescriptor() = default;

        uint64_t m_byteCount = 0;

        uint64_t m_alignment = 0;

        BufferBindFlags m_bindFlags = BufferBindFlags::None;

        HardwareQueueClassMask m_sharedQueueMask = HardwareQueueClassMask::All;
    };
    
    
}