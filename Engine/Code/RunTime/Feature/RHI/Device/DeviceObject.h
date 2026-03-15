#pragma once

#include <Object/Object.h>

#include "Device.h"
#include "DeviceObjectPool.h"

namespace Spark::RHI
{
    class DeviceObject: public Object
    {
        friend class Factory;  // Construct DeviceObjects
    public:
        virtual ~DeviceObject() = default;

        bool IsInitialized() const;

        Device& GetDevice() const;

        void RegisterDeviceObjectPool(DeviceObjectPoolBase* pool);

        DeviceObjectPoolBase* GetObjectPool() const;

    protected:
        DeviceObject() = default;

        void Init(Device& device);

        // 子类重写时需要调用此Shutdown
        void Shutdown() override;

    private:
        Ptr<Device> m_device = nullptr;
        DeviceObjectPoolBase* m_objectPool;
    };
}