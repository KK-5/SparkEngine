#pragma once

#include <EASTL/vector.h>
#include <Object/Object.h>

#include "PhysicalDeviceDescriptor.h"


namespace Spark::RHI
{
    using PhysicalDeviceList = eastl::vector<Ptr<PhysicalDevice>>;

    class PhysicalDevice : public Object
    {
    public:
        virtual ~PhysicalDevice() = default;
        const PhysicalDeviceDescriptor& GetDescriptor() const;

        virtual PhysicalDeviceList Enumerate() = 0;

    protected:
        PhysicalDeviceDescriptor m_descriptor;
    };

    //using PhysicalDeviceList = eastl::vector<Ptr<PhysicalDevice>>;
}

