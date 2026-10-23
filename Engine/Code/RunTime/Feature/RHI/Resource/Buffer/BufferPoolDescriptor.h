#pragma once

#include <RHI/MemoryEnums.h>
#include <RHI/HardwareQueue.h>
#include <RHI/Resource/ResourcePoolDescriptor.h>

#include "BufferBindFlags.h"

namespace Spark::RHI
{
    struct BufferPoolDescriptor : public ResourcePoolDescriptor
    {
        BufferPoolDescriptor() = default;
        virtual ~BufferPoolDescriptor() = default;

        HeapMemoryLevel m_heapMemoryLevel = HeapMemoryLevel::Device;

        HostMemoryAccess m_hostMemoryAccess = HostMemoryAccess::Write;

        BufferBindFlags m_bindFlags = BufferBindFlags::None;

        //! Size of each memory block the pool allocates from. 0 lets the allocator size them and
        //! give large buffers memory of their own; any other value fixes it, every buffer is
        //! placed in a block, and one larger than a block fails.
        uint64_t m_blockSizeInBytes = 0;

        //! Least alignment of every buffer of the pool, beyond what the buffer itself needs. 0 adds none.
        uint64_t m_minAllocationAlignment = 0;

        HardwareQueueClassMask m_sharedQueueMask = HardwareQueueClassMask::All;
    };
}