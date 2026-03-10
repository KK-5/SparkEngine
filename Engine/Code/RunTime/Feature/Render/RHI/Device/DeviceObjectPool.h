#pragma once

#include <RHI/Base.h>

#include "DeviceObjectFactory.h"

namespace Spark::RHI
{
    class DeviceObject;

    class DeviceObjectPoolBase
    {
    public:
        virtual ~DeviceObjectPoolBase() = default;

        ResultCode Init();

        void Shutdown();

        DeviceObject* CreateDeviceObject();

        void QueueForRelease(DeviceObject* object);

        virtual void Collect() = 0;

    protected:
        virtual ResultCode InitInternal() = 0;

        virtual void ShutdownInternal() = 0;

        virtual DeviceObject* CreateDeviceObjectInternal() = 0;

        virtual void QueueForReleaseInternal(DeviceObject* object) = 0;
    };
}