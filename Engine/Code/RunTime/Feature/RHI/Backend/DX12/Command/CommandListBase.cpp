/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Remove m_aftermathCommandListContext in CommandListBase.
 */

#include "CommandListBase.h"

#include <Log/ILogSystem.h>

#include <Device/Device.h>
#include <Conversions.h>

namespace Spark::RHI::DX12
{
    void CommandListBase::Init(Device& device, RHI::HardwareQueueClass hardwareQueueClass, ID3D12CommandAllocator* commandAllocator)
    {
        DeviceObject::Init(device);
        m_hardwareQueueClass = hardwareQueueClass;

        HRESULT hr = device.GetDX12Device()->CreateCommandList(1, ConvertHardwareQueueClass(hardwareQueueClass), commandAllocator, nullptr, IID_PPV_ARGS(m_commandList.ReleaseAndGetAddressOf()));
        ASSERT(SUCCEEDED(hr), "[DX12 CommandListBase] CreateCommandList failed!");
        m_isRecording = true;

        /*
        if (device.IsAftermathInitialized())
        {
            m_aftermathCommandListContext = Aftermath::CreateAftermathContextHandle(GetCommandList(), device.GetAftermathGPUCrashTracker());
        }
        */
    }

    void CommandListBase::SetBarrierState(const RHI::MultisampleState& state)
    {
        SetSamplePositions(state);
    }

    void CommandListBase::SetSamplePositions(const RHI::MultisampleState& multisampleState)
    {
        if (multisampleState.m_customPositionsCount == m_baseState.m_customSamplePositions.m_customPositionsCount &&
            multisampleState.m_samples == m_baseState.m_customSamplePositions.m_samples &&
            ::memcmp(
                multisampleState.m_customPositions.data(),
                m_baseState.m_customSamplePositions.m_customPositions.data(),
                sizeof(decltype(multisampleState.m_customPositions)::value_type) * multisampleState.m_customPositionsCount) == 0)
        {
            return;
        }

        if (multisampleState.m_customPositionsCount > 0)
        {
            ASSERT(GetDevice().GetFeatures().m_customSamplePositions, "Custom sample positions are not supported on this device");
            eastl::vector<D3D12_SAMPLE_POSITION> samplePositions;
            eastl::transform(
                multisampleState.m_customPositions.begin(),
                multisampleState.m_customPositions.begin() + multisampleState.m_customPositionsCount,
                eastl::back_inserter(samplePositions),
                [&](const auto& item)
                {
                    return ConvertSamplePosition(item);
                });
            m_commandList->SetSamplePositions(multisampleState.m_samples, 1, samplePositions.data());
        }
        else
        {
            m_commandList->SetSamplePositions(0, 0, nullptr);
        }
        m_baseState.m_customSamplePositions = multisampleState;
    }

    void CommandListBase::Reset(ID3D12CommandAllocator* commandAllocator)
    {
        ASSERT(m_queuedBarriers.empty(), "Unflushed barriers in command list.");

        m_commandList->Reset(commandAllocator, nullptr);
        m_baseState = State();
        m_isRecording = true;
    }

    void CommandListBase::Close()
    {
        ASSERT(m_isRecording, "Attempting to close command list that isn't in a recording state");
        m_isRecording = false;
        HRESULT hr = m_commandList->Close();
        ASSERT(SUCCEEDED(hr), "Close CommandlList failed!");
    }

    bool CommandListBase::IsRecording() const
    {
        return m_isRecording;
    }

    ID3D12GraphicsCommandListX* CommandListBase::GetCommandList()
    {
        return m_commandList.Get();
    }

    const ID3D12GraphicsCommandListX* CommandListBase::GetCommandList() const
    {
        return m_commandList.Get();
    }

    RHI::HardwareQueueClass CommandListBase::GetHardwareQueueClass() const
    {
        return m_hardwareQueueClass;
    }

    void CommandListBase::FlushBarriers()
    {
        if (m_queuedBarriers.size())
        {
            // Some barriers needs a specific state before being emitted (e.g. Depth/Stencil resources with custom sample locations).
            // We first search for barriers using the same state, then set the state in the commandlist and finally emit the barriers.
            auto beginIt = m_queuedBarriers.begin();
            decltype(beginIt) endIt;
            eastl::vector<D3D12_RESOURCE_BARRIER> barriers(m_queuedBarriers.size());
            decltype(BarrierOp::m_cmdListState) currentState;
            do
            {
                // Find the first barrier that contains a different state
                endIt = eastl::find_if(
                    beginIt + 1,
                    m_queuedBarriers.end(),
                    [&](const auto& barrier)
                    {
                        return barrier.m_cmdListState != currentState;
                    });

                // Adds the barriers that use the same state to the list
                barriers.clear();
                eastl::transform(
                    beginIt,
                    endIt,
                    eastl::back_inserter(barriers),
                    [&](const auto& item)
                    {
                        return item.m_barrier;
                    });

                currentState = beginIt->m_cmdListState;
                beginIt = endIt;

                // Set the state needed by the barriers
                if (currentState)
                {
                    SetBarrierState(currentState.value());
                }
                m_commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
            } while (endIt != m_queuedBarriers.end());
            
            m_queuedBarriers.clear();
        }
    }

    void CommandListBase::QueueAliasingBarrier(
        const D3D12_RESOURCE_ALIASING_BARRIER& barrier,
        const RHI::MultisampleState* state /*=nullptr*/)
    {
        m_queuedBarriers.emplace_back();
        BarrierOp& barrierOp = m_queuedBarriers.back();

        D3D12_RESOURCE_BARRIER& barrierDesc = barrierOp.m_barrier;
        barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrierDesc.Aliasing = barrier;

        if (state)
        {
            barrierOp.m_cmdListState.emplace(*state);
        }
    }

    void CommandListBase::QueueAliasingBarrier(const BarrierOp& op)
    {
        QueueAliasingBarrier(op.m_barrier.Aliasing, op.m_cmdListState.has_value() ? &op.m_cmdListState.value() : nullptr);
    }

    void CommandListBase::QueueTransitionBarrier(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES stateBefore,
        D3D12_RESOURCE_STATES stateAfter,
        const RHI::MultisampleState* state /*=nullptr*/)
    {
        D3D12_RESOURCE_TRANSITION_BARRIER barrier;
        barrier.pResource = resource;
        barrier.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.StateBefore = stateBefore;
        barrier.StateAfter = stateAfter;
        QueueTransitionBarrier(barrier, state);
    }

    void CommandListBase::QueueTransitionBarrier(
        const D3D12_RESOURCE_TRANSITION_BARRIER& transitionBarrier,
        const RHI::MultisampleState* state /*=nullptr*/)
    {
        if (transitionBarrier.StateBefore != transitionBarrier.StateAfter)
        {
            m_queuedBarriers.emplace_back();

            BarrierOp& barrierOp = m_queuedBarriers.back();
            D3D12_RESOURCE_BARRIER& barrierDesc = barrierOp.m_barrier;
            barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrierDesc.Transition = transitionBarrier;

            if (state)
            {
                barrierOp.m_cmdListState.emplace(*state);
            }
        }
        else if (transitionBarrier.StateBefore == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            m_queuedBarriers.emplace_back();

            BarrierOp& barrierOp = m_queuedBarriers.back();
            D3D12_RESOURCE_BARRIER& barrierDesc = barrierOp.m_barrier;
            barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrierDesc.UAV.pResource = transitionBarrier.pResource;

            if (state)
            {
                barrierOp.m_cmdListState.emplace(*state);
            }
        }
    }

    void CommandListBase::QueueTransitionBarrier(const BarrierOp& op)
    {
        QueueTransitionBarrier(op.m_barrier.Transition, op.m_cmdListState.has_value() ? &op.m_cmdListState.value() : nullptr);
    }
}