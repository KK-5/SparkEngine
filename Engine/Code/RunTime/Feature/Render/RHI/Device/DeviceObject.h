#pragma once

#include <Object/Object.h>
#include <Memory/PoolAllocator.h>

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

        PoolAllocatorBase* GetAllocator() const;

    protected:
        DeviceObject() = default;

        void Init(Device& device, PoolAllocatorBase* allocator = nullptr);

        void SetAllocator(PoolAllocatorBase* allocator);

        // 子类重写时需要调用此Shutdown
        void Shutdown() override;

    private:
        Ptr<Device> m_device = nullptr;
        PoolAllocatorBase* m_allcator = nullptr;
    };
}