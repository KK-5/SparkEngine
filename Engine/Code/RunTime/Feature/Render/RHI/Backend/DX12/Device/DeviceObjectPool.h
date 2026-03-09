#pragma once

#include <Object/ObjectPool.h>

#include <RHI/Device/DeviceObjectFactory.h>

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

    using BufferObjectPool = ObjectPool<BufferObjectPoolTraits>;
}