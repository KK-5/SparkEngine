#include "ISystem.h"

#include <Log/SpdLogSystem.h>

namespace Spark
{
    void ISystem::Init()
    {
        if (m_initialized)
        {
            LOG_ERROR("[ISystem] System {} is already initialized.", GetName().data());
            return;
        }

        InitInternal();
        m_initialized = true;
    }

    void ISystem::Shutdown()
    {
        if (!m_initialized)
        {
            LOG_ERROR("[ISystem] System {} is not initialized.", GetName().data());
            return;
        }

        ShutdownInternal();
        m_initialized = false;
    }
}