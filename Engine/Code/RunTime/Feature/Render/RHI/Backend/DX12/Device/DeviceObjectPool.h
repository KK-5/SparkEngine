#pragma once

#include <Object/ObjectPool.h>

#include <RHI/Device/DeviceObjectFactory.h>
#include <RHI/Device/DeviceObjectPool.h>

#include <Resource/Buffer/Buffer.h>

namespace Spark::RHI::DX12
{
    using BufferObjectFactory = RHI::DeviceObjectFactory<Buffer>;

    struct BufferObjectPoolTraits : public ObjectPoolTraits
    {
        using ObjectType = Buffer;
        using ObjectFactoryType = BufferObjectFactory;
        using MutexType = std::mutex;
    };

    using BufferObjectPoolInternal = ObjectPool<BufferObjectPoolTraits>;

    class BufferObjectPool : public RHI::DeviceObjectPoolBase
    {
    public:
        RHI::ResultCode Init();

        void Shutdown();

        Buffer* CreateDeviceObject() override;

        void QueueForRelease(DeviceObject* buffer) override;

    private:
        BufferObjectPoolInternal m_internalPool;
    };
}