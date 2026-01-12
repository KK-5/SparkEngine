#include "DeviceObject.h"

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
        auto factory = Service<Factory>::Get();
        assert(factory);
        DeviceObject* obj = static_cast<DeviceObject*>(this);
        factory->DestoryObject(obj);
    }
}