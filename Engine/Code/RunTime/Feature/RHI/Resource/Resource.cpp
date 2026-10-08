#include "Resource.h"

#include <Log/ILogSystem.h>

#include "ResourcePool.h"

namespace Spark::RHI
{
    Resource::~Resource()
    {
        if (m_pool != nullptr)
        {
            LOG_ERROR("[Resource] Resource {} is still registered on pool. {}", GetName().GetCStr(), m_pool->GetName().GetCStr());
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

    void Resource::SetPool(ResourcePool* pool)
    {
        m_pool = pool;
    }
}