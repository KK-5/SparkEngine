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

    PoolAllocatorBase* DeviceObject::GetAllocator() const
    {
        return m_allcator;
    }

    void DeviceObject::SetAllocator(PoolAllocatorBase* allocator)
    {
        m_allcator = allocator;
    }

    void DeviceObject::Init(Device& device, PoolAllocatorBase* allocator)
    {
        m_device = &device;
        m_allcator = allocator;
    }

    void DeviceObject::Shutdown()
    {
        m_device = nullptr;
        if (m_allcator)
        {
            // 显式调用析构函数，通常资源清理已在Shutdown中完成，析构函数无需清理资源
            this->~DeviceObject();
            m_allcator->deallocate(this);
        }
        else
        {
            delete this;
        }
    }
}