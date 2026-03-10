#pragma once

#include <Object/ObjectPool.h>

#include <RHI/Device/DeviceObjectFactory.h>
#include <RHI/Device/DeviceObjectPool.h>

namespace Spark::RHI::DX12
{
    namespace Internal
    {
        template <typename T>
        using DeviceObjectFactory = RHI::DeviceObjectFactory<T>;

        template <typename T>
        struct DeviceObjectPoolTraits : public ObjectPoolTraits
        {
            using ObjectType = T;
            using ObjectFactoryType = DeviceObjectFactory<T>;
            using MutexType = std::mutex;
        };

        template <typename T>
        using DeviceObjectPool = ObjectPool<DeviceObjectPoolTraits<T>>;
    }
    
    template <typename T>
    class DeviceObjectPool final : public DeviceObjectPoolBase
    {
    public:
        ~DeviceObjectPool() = default;

        ResultCode InitInternal() override
        {
            Internal::DeviceObjectPool<T>::Descriptor desc;
            desc.m_collectLatency = 0;

            m_internalPool.Init(desc);

            return ResultCode::Success;
        }

        void ShutdownInternal() override
        {
            m_internalPool.Shutdown();
        }

        DeviceObject* CreateDeviceObjectInternal() override
        {
            T* object = m_internalPool.Allocate();
            return object;
        }

        void QueueForReleaseInternal(DeviceObject* object) override
        {
            T* ptr = static_cast<T*>(object);
            m_internalPool.DeAllocate(ptr);
        }

        void Collect() override
        {
            m_internalPool.Collect();
        }

    private:
        Internal::DeviceObjectPool<T> m_internalPool;
    };

}