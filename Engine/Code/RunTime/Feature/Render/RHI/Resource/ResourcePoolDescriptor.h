#pragma once

#include <cstdint>
#include <mutex>
#include <Object/ObjectPool.h>

namespace Spark::Render::RHI
{
    class Resource;

    struct ResourcePoolTraits : ObjectPoolTraits
    {
        using ObjectType = Resource;

        using MutexType = std::shared_mutex;
    };

    struct ResourcePoolDescriptor : ObjectPool<ResourcePoolTraits>::Descriptor
    {
        ResourcePoolDescriptor() = default;
        virtual ~ResourcePoolDescriptor() = default;

        uint64_t m_budgetInBytes = 0;
    };
}