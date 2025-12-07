#include "Resource.h"

#include <Log/SpdLogSystem.h>

#include "ResourcePool.h"

namespace Spark::Render::RHI
{
    Resource::~Resource()
    {
        if (GetPool() == nullptr)
        {
            LOG_ERROR("[Resource] Resource {} is still registered on pool. {}", GetName().GetCStr(), GetPool()->GetName().GetCStr());
        }
    }

    void Resource::Shutdown()
    {
        if (m_pool)
        {
            if (m_frameAttachment)
            {
                LOG_ERROR("[Resource] Resource {} is still assigned to a frame attachment during shutdown.", GetName().GetCStr());
            }
            m_pool->ShutdownResource(this);
        }
        DeviceObject::Shutdown();
    }

    bool Resource::IsAttachment() const
    {
        return m_frameAttachment != nullptr;
    }

    const ResourcePool* Resource::GetPool() const
    {
        return m_pool;
    }

    ResourcePool* Resource::GetPool()
    {
        return m_pool;
    }

    const FrameAttachment* Resource::GetFrameAttachment() const
    {
        return m_frameAttachment;
    }

    void Resource::SetFrameAttachment(FrameAttachment* frameAttachment)
    {
        if (Validation::isEnabled)
        {
            if (m_frameAttachment && frameAttachment)
            {
                LOG_ERROR("[Resource] Resource {} already has a frame attachment assigned.", GetName().GetCStr());
                return;
            }
            if (!m_frameAttachment && !frameAttachment)
            {
                LOG_ERROR("[Resource] Resource {} does not have a frame attachment assigned.", GetName().GetCStr());
                return;
            }
        }

        m_frameAttachment = frameAttachment;
    }

    void Resource::SetPool(ResourcePool* pool)
    {
        m_pool = pool;
    }
}