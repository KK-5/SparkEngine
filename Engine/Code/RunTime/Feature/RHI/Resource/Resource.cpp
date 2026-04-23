#include "Resource.h"

#include <Log/SpdLogSystem.h>

#include "ResourcePool.h"

namespace Spark::RHI
{
    Resource::~Resource()
    {
        if (GetPool() != nullptr)
        {
            LOG_ERROR("[Resource] Resource {} is still registered on pool. {}", GetName().GetCStr(), GetPool()->GetName().GetCStr());
        }
    }

    void Resource::Shutdown()
    {
        if (m_pool)
        {
            m_pool->ShutdownResource(this);
        }

        DeviceObject::Shutdown();
    }

    const ResourcePool* Resource::GetPool() const
    {
        return m_pool;
    }

    ResourcePool* Resource::GetPool()
    {
        return m_pool;
    }

    void Resource::SetPool(ResourcePool* pool)
    {
        m_pool = pool;
    }

    ResourceState Resource::GetResourceState() const
    {
        return m_resourceState;
    }

    void Resource::SetResourceState(ResourceState state)
    {
        m_resourceState = state;
    }
}