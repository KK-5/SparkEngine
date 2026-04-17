#include "DeviceObject.h"

#include <Log/SpdLogSystem.h>
#include <Service/Service.h>
#include <RHI/Factory.h>

namespace Spark::RHI
{
    bool DeviceObject::IsInitialized() const
    {
        return m_device != nullptr;
    }

    Device& DeviceObject::GetDevice() const
    {
        return *m_device;
    }

    void DeviceObject::Init(Device& device)
    {
        m_device = &device;
    }

    void DeviceObject::Shutdown()
    {
        m_device = nullptr;
        if (m_objectPool)
        {
            m_objectPool->QueueForRelease(this);
        }
        /*
        else
        {
            LOG_ERROR("[DeviceObject] There is a device object have not registered to pool.");
        }
        */
    }

    void DeviceObject::RegisterDeviceObjectPool(DeviceObjectPoolBase* pool)
    {
        m_objectPool = pool;
    }

    DeviceObjectPoolBase* DeviceObject::GetObjectPool() const
    {
        return m_objectPool;
    }
}