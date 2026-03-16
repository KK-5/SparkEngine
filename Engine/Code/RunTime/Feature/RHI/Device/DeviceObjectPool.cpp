#include "DeviceObjectPool.h"

#include <Log/SpdLogSystem.h>

#include "DeviceObject.h"

namespace Spark::RHI
{
    ResultCode DeviceObjectPoolBase::Init()
    {
        return InitInternal();
    }

    void DeviceObjectPoolBase::Shutdown()
    {
        return ShutdownInternal();
    }

    DeviceObject* DeviceObjectPoolBase::CreateDeviceObject()
    {
        DeviceObject* object = CreateDeviceObjectInternal();
        if (object)
        {
            object->RegisterDeviceObjectPool(this);
        }
        return object;
    }

    void DeviceObjectPoolBase::QueueForRelease(DeviceObject* object)
    {
        ASSERT(object && object->GetObjectPool(), "[DeviceObjectPoolBase] Invalid release operation");
        if (object->GetObjectPool() != this)
        {
            LOG_ERROR("[DeviceObjectPoolBase] Attempt to release the object in an unrelated object pool");
            return;
        }

        QueueForReleaseInternal(object);
    }
}