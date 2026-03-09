#pragma once

#include <Object/Object.h>

#include "Device.h"

namespace Spark::RHI
{
    class DeviceObject: public Object
    {
        friend class Factory;  // Construct DeviceObjects
    public:
        virtual ~DeviceObject() = default;

        bool IsInitialized() const;

        Device& GetDevice() const;

    protected:
        DeviceObject() = default;

        void Init(Device& device);

        template<typename T>
        void DeAllocateThis(T& pool);

        // 子类重写时需要调用此Shutdown
        void Shutdown() override;

    private:
        Ptr<Device> m_device = nullptr;
    };

    template<typename T>
    void DeviceObject::DeAllocateThis(T& pool)
    {
        pool.DeAllocate(this);
    }
}