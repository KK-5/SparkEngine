#include "RHISystem.h"

#include <Log/SpdLogSystem.h>

namespace Spark::RHI::DX12
{
    void RHISystem::InitInternal()
    {
        m_rhiFactory = eastl::make_unique<ID3D12Factory>();
        RHI::ResultCode res =  m_rhiFactory->Init();
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Failed to initialize ID3D12Factory!");
        }
    }

    void RHISystem::ShutdownInternal()
    {
        if (m_rhiFactory)
        {
            m_rhiFactory->Shutdown();
        }
    }

    void RHISystem::OnFrameEnd()
    {
        if (m_rhiFactory)
        {
            m_rhiFactory->Collect();
        }
    }
}