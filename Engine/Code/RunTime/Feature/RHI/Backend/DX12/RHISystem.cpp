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
        FrameEventBus::Handler::BusConnect();

        // Chain to base: push the engine-wide RHIContext onto the execute
        // context stack now that the backend is fully bootstrapped.
        RHIInterface::InitInternal();
    }

    void RHISystem::ShutdownInternal()
    {
        // Shut down the factory first so that all D3D12 resources (descriptor
        // heaps, command queues, fences, pipeline states, root signatures,
        // command allocators, command lists, D3D12MA allocations) are released
        // before Device::ShutdownInternal calls ReportLiveDeviceObjects.
        if (m_rhiFactory)
        {
            m_rhiFactory->Shutdown();
        }

        RHIInterface::ShutdownInternal();

        FrameEventBus::Handler::BusDisconnect();
    }

    void RHISystem::OnFrameBegin()
    {
        ASSERT(m_rhiFactory, "RHI Factory is invalid.");
        m_rhiFactory->BeginFrame();
    }

    void RHISystem::OnFrameEnd()
    {
        ASSERT(m_rhiFactory, "RHI Factory is invalid.");
        m_rhiFactory->EndFrame();
    }
}