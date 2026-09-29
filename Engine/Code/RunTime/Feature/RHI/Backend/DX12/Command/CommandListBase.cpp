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
        if (m_queuedBarriers.empty())
        {
            return;
        }

        // Some barriers needs a specific state before being emitted (e.g. Depth/Stencil resources with custom sample locations).
        // Each run of barriers sharing a state is emitted as one Barrier() call, one group per barrier type.
        eastl::vector<D3D12_GLOBAL_BARRIER>  globals;
        eastl::vector<D3D12_TEXTURE_BARRIER> textures;
        eastl::vector<D3D12_BUFFER_BARRIER>  buffers;
        auto beginIt = m_queuedBarriers.begin();
        while (beginIt != m_queuedBarriers.end())
        {
            const auto& currentState = beginIt->m_cmdListState;
            const auto endIt = eastl::find_if(
                beginIt + 1,
                m_queuedBarriers.end(),
                [&](const BarrierOp& op)
                {
                    return op.m_cmdListState != currentState;
                });

            globals.clear();
            textures.clear();
            buffers.clear();
            for (auto it = beginIt; it != endIt; ++it)
            {
                switch (it->m_type)
                {
                case D3D12_BARRIER_TYPE_GLOBAL:
                    globals.push_back(it->m_global);
                    break;
                case D3D12_BARRIER_TYPE_TEXTURE:
                    textures.push_back(it->m_texture);
                    break;
                case D3D12_BARRIER_TYPE_BUFFER:
                    buffers.push_back(it->m_buffer);
                    break;
                }
            }

            eastl::fixed_vector<D3D12_BARRIER_GROUP, 3, false> groups;
            if (!globals.empty())
            {
                D3D12_BARRIER_GROUP& group = groups.push_back();
                group.Type            = D3D12_BARRIER_TYPE_GLOBAL;
                group.NumBarriers     = static_cast<UINT32>(globals.size());
                group.pGlobalBarriers = globals.data();
            }
            if (!textures.empty())
            {
                D3D12_BARRIER_GROUP& group = groups.push_back();
                group.Type             = D3D12_BARRIER_TYPE_TEXTURE;
                group.NumBarriers      = static_cast<UINT32>(textures.size());
                group.pTextureBarriers = textures.data();
            }
            if (!buffers.empty())
            {
                D3D12_BARRIER_GROUP& group = groups.push_back();
                group.Type            = D3D12_BARRIER_TYPE_BUFFER;
                group.NumBarriers     = static_cast<UINT32>(buffers.size());
                group.pBufferBarriers = buffers.data();
            }

            if (currentState)
            {
                SetBarrierState(currentState.value());
            }
            m_commandList->Barrier(static_cast<UINT32>(groups.size()), groups.data());
            beginIt = endIt;
        }

        m_queuedBarriers.clear();
    }

    void CommandListBase::QueueTextureBarrier(
        const D3D12_TEXTURE_BARRIER& barrier,
        const RHI::MultisampleState* state /*=nullptr*/)
    {
        BarrierOp& barrierOp = m_queuedBarriers.emplace_back();
        barrierOp.m_type    = D3D12_BARRIER_TYPE_TEXTURE;
        barrierOp.m_texture = barrier;
        if (state)
        {
            barrierOp.m_cmdListState.emplace(*state);
        }
    }

    void CommandListBase::QueueBufferBarrier(const D3D12_BUFFER_BARRIER& barrier)
    {
        BarrierOp& barrierOp = m_queuedBarriers.emplace_back();
        barrierOp.m_type   = D3D12_BARRIER_TYPE_BUFFER;
        barrierOp.m_buffer = barrier;
    }

    void CommandListBase::QueueGlobalBarrier(const D3D12_GLOBAL_BARRIER& barrier)
    {
        BarrierOp& barrierOp = m_queuedBarriers.emplace_back();
        barrierOp.m_type   = D3D12_BARRIER_TYPE_GLOBAL;
        barrierOp.m_global = barrier;
    }
}