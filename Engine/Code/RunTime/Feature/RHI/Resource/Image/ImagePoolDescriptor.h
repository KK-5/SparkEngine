#pragma once

#include <RHI/MemoryEnums.h>
#include <RHI/Resource/ResourcePoolDescriptor.h>
#include "ImageEnums.h"

namespace Spark::RHI
{
    class ImagePoolDescriptor: public ResourcePoolDescriptor
    {
    public:
        virtual ~ImagePoolDescriptor() = default;

        ImagePoolDescriptor() = default;

        HeapMemoryLevel m_heapMemoryLevel = HeapMemoryLevel::Device;

        HostMemoryAccess m_hostMemoryAccess = HostMemoryAccess::Write;

        ImageBindFlags m_bindFlags = ImageBindFlags::Color;
    };
}