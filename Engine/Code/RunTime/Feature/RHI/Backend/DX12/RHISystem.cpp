#include "RHISystem.h"

#include <Log/ILogSystem.h>

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
        // Step 1: Destroy all RHI entities while the factory (and its pools /
        // release queues) are still alive. Component destructors return D3D12
        // resources to their pools; pools queue allocations for release.
        RHIInterface::ShutdownInternal();

        // Step 2: Shut down the factory. This destroys every pool → every
        // D3D12MA allocator → every remaining allocation and D3D12 resource.
        // After this point no D3D12 resources remain.
        if (m_rhiFactory)
        {
            m_rhiFactory->Shutdown();
        }

        // Step 3: Release the logical device. Device::ShutdownInternal calls
        // ReportLiveDeviceObjects, which must find zero live D3D12 objects.
        m_device.reset();

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