#pragma once

#include "DeviceObject.h"
#include "DeviceObjectFactory.h"

namespace Spark::RHI
{
    class DeviceObjectPoolBase
    {
    public:
        virtual ~DeviceObjectPoolBase() = default;

        virtual DeviceObject* CreateDeviceObject() = 0;

        virtual void QueueForRelease(DeviceObject* object) = 0;
    };
}