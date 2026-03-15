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

        uint64_t m_largestPooledAllocationSizeInBytes = 0;

        HardwareQueueClassMask m_sharedQueueMask = HardwareQueueClassMask::All;
    };
}