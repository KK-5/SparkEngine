#pragma once

#include <EASTL/vector.h>
#include <Object/Object.h>

#include "PhysicalDeviceDescriptor.h"


namespace Spark::Render::RHI
{
    class PhysicalDevice : public Object
    {
    public:
        virtual ~PhysicalDevice() = default;
        const PhysicalDeviceDescriptor& GetDescriptor() const;

    protected:
        PhysicalDeviceDescriptor m_descriptor;
    };

    using PhysicalDeviceList = eastl::vector<Ptr<PhysicalDevice>>;
}

