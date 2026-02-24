/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "Fence.h"

#include <Log/SpdLogSystem.h>

#include <Device/Device.h>

namespace Spark::RHI::DX12
{
    FenceEvent::FenceEvent(const char* name)
        : m_EventHandle(nullptr)
        , m_name(name)
    {
        m_EventHandle = CreateEvent(nullptr, false, false, nullptr);
    }

    FenceEvent::~FenceEvent()
    {
        CloseHandle(m_EventHandle);
    }

    const char* FenceEvent::GetName() const
    {
        return m_name;
    }

    RHI::ResultCode D3D12Fence::Init(ID3D12DeviceX* dx12Device, RHI::FenceState initialState)
    {
        Microsoft::WRL::ComPtr<ID3D12Fence> fencePtr;
        D3D12_FENCE_FLAGS flags = D3D12_FENCE_FLAG_NONE;

        HRESULT hr = dx12Device->CreateFence(0, flags, IID_PPV_ARGS(fencePtr.GetAddressOf()));

        if (!SUCCEEDED(hr))
        {
            LOG_ERROR("[D3D12Fence] Failed to initialize ID3D12Fence.");
            return RHI::ResultCode::Fail;
        }

        m_fence = fencePtr.Get();
        m_fence->Signal(0);
        m_pendingValue = (initialState == RHI::FenceState::Signaled) ? 0 : 1;
        return RHI::ResultCode::Success;
    }

    void D3D12Fence::Shutdown()
    {
        m_fence = nullptr;
    }

    void D3D12Fence::Wait(FenceEvent& fenceEvent) const
    {
        Wait(fenceEvent, GetPendingValue());
    }

    void D3D12Fence::Wait(FenceEvent& fenceEvent, uint64_t fenceValue) const
    {
        if (fenceValue > GetCompletedValue())
        {
            m_fence->SetEventOnCompletion(fenceValue, fenceEvent.m_EventHandle);
            WaitForSingleObject(fenceEvent.m_EventHandle, INFINITE);
        }
    }

    void D3D12Fence::Signal()
    {
        m_fence->Signal(GetPendingValue());
    }

    RHI::FenceState D3D12Fence::GetFenceState() const
    {
        const uint64_t completedValue = GetCompletedValue();
        return (m_pendingValue <= completedValue) ? RHI::FenceState::Signaled : RHI::FenceState::Reset;
    }

    uint64_t D3D12Fence::GetPendingValue() const
    {
        return m_pendingValue;
    }

    uint64_t D3D12Fence::GetCompletedValue() const
    {
        return m_fence->GetCompletedValue();
    }

    ID3D12Fence* D3D12Fence::Get() const
    {
        return m_fence.get();
    }

    void FenceSet::Init(ID3D12DeviceX* dx12Device, RHI::FenceState initialState)
    {
        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            m_fences[hardwareQueueIdx].Init(dx12Device, initialState);
        }
    }

    void FenceSet::Shutdown()
    {
        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            m_fences[hardwareQueueIdx].Shutdown();
        }
    }

    void FenceSet::Wait(FenceEvent& event) const
    {
        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            m_fences[hardwareQueueIdx].Wait(event);
        }
    }

    void FenceSet::Reset()
    {
        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            m_fences[hardwareQueueIdx].Increment();
        }
    }

    D3D12Fence& FenceSet::GetD3D12Fence(RHI::HardwareQueueClass hardwareQueueClass)
    {
        return m_fences[static_cast<uint32_t>(hardwareQueueClass)];
    }

    const D3D12Fence& FenceSet::GetD3D12Fence(RHI::HardwareQueueClass hardwareQueueClass) const
    {
        return m_fences[static_cast<uint32_t>(hardwareQueueClass)];
    }

    RHI::ResultCode Fence::InitInternal(RHI::Device& deviceBase, RHI::FenceState initialState)
    {
        return m_fence.Init(static_cast<Device&>(deviceBase).GetDevice(), initialState);
    }

    void Fence::ShutdownInternal()
    {
        m_fence.Shutdown();
    }

    void Fence::ResetInternal()
    {
        m_fence.Increment();
    }

    void Fence::SignalOnCpuInternal()
    {
        m_fence.Signal();
    }

    void Fence::WaitOnCpuInternal() const
    {
        FenceEvent event("WaitOnCpu");
        m_fence.Wait(event);
    }

    RHI::FenceState Fence::GetFenceStateInternal() const
    {
        return m_fence.GetFenceState();
    }
}