/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "CommandQueueContext.h"

#include <RHI/Factory.h>
#include <RHI/Fence/Fence.h>

#include <Log/ILogSystem.h>

namespace Spark::RHI
{
    //////////////////////////////////////////////////////////////////
    /// FenceSet
    void FenceSet::Init(RHI::Device& device, RHI::FenceState initialState)
    {
        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            m_fences[hardwareQueueIdx] = Service<Factory>::Get()->CreateFence();
            ResultCode result = m_fences[hardwareQueueIdx]->Init(device, initialState);
            if (result != ResultCode::Success)
            {
                LOG_ERROR("[FenceSet] Initialize fence failed.");
                return;
            }
        }
    }

    void FenceSet::Shutdown()
    {
        // eastl container auto release
    }

    void FenceSet::Wait() const
    {
        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            ResultCode result = m_fences[hardwareQueueIdx]->WaitOnCpu();
            if (result != ResultCode::Success)
            {
                LOG_ERROR("[FenceSet] Fence wait failed.");
                return;
            }
        }
    }

    void FenceSet::Reset()
    {
        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            ResultCode result = m_fences[hardwareQueueIdx]->Reset();
            if (result != ResultCode::Success)
            {
                LOG_ERROR("[FenceSet] Fence reset failed.");
                return;
            }
        }
    }
    
    RHI::Fence& FenceSet::GetFence(RHI::HardwareQueueClass hardwareQueueClass)
    {
        return *m_fences[static_cast<uint32_t>(hardwareQueueClass)];
    }

    const RHI::Fence& FenceSet::GetFence(RHI::HardwareQueueClass hardwareQueueClass) const
    {
        return *m_fences[static_cast<uint32_t>(hardwareQueueClass)];
    }
    //////////////////////////////////////////////////////////////////

    RHI::ResultCode CommandQueueContext::Init(RHI::Device& device)
    {
        m_device = &device;
        m_currentFrameIndex = 0;
        m_frameFences.resize(device.GetDescriptor().m_frameCountMax);
        for (FenceSet& fences : m_frameFences)
        {
            fences.Init(device, RHI::FenceState::Signaled);
        }

        m_flushFences.Init(device, RHI::FenceState::Reset);

        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            m_commandQueues[hardwareQueueIdx] = Service<RHI::Factory>::Get()->CreateCommandQueue();

            RHI::CommandQueueDescriptor commandQueueDesc;
            commandQueueDesc.m_hardwareQueueClass = static_cast<RHI::HardwareQueueClass>(hardwareQueueIdx);
            ResultCode result = m_commandQueues[hardwareQueueIdx]->Init(device, commandQueueDesc);
            if (result != ResultCode::Success)
            {
                LOG_ERROR("[CommandQueueContext] Init commandqueue failed.");
                return result;
            }
        }

        return ResultCode::Success;
    }

    void CommandQueueContext::Shutdown()
    {
        // eastl container auto release
    }

    CommandQueue& CommandQueueContext::GetCommandQueue(RHI::HardwareQueueClass hardwareQueueClass)
    {
        return *m_commandQueues[static_cast<uint32_t>(hardwareQueueClass)];
    }

    const CommandQueue& CommandQueueContext::GetCommandQueue(RHI::HardwareQueueClass hardwareQueueClass) const
    {
        return *m_commandQueues[static_cast<uint32_t>(hardwareQueueClass)];
    }

    void CommandQueueContext::Begin()
    {

    }

    void CommandQueueContext::IncrementFlushFence(RHI::HardwareQueueClass hardwareQueueClass)
    {
        ResultCode result = m_flushFences.GetFence(hardwareQueueClass).Reset();
        if (result != ResultCode::Success)
        {
            LOG_ERROR("[CommandQueueContext] Compile fence reset failed.");
            return;
        }
    }

    void CommandQueueContext::SignalOnGpu(FenceSet& fenceSet)
    {
        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            const RHI::HardwareQueueClass hardwareQueueClass = static_cast<RHI::HardwareQueueClass>(hardwareQueueIdx);
            RHI::Fence& fence = fenceSet.GetFence(hardwareQueueClass);
            m_commandQueues[hardwareQueueIdx]->Signal(fence);
        }
    }

    void CommandQueueContext::FlushCommands(RHI::HardwareQueueClass hardwareQueueClass)
    {
        ResultCode result = GetCommandQueue(hardwareQueueClass).FlushCommands(m_flushFences.GetFence(hardwareQueueClass));
        if (result != ResultCode::Success)
        {
            LOG_ERROR("[CommandQueueContext] Wait compile fence failed.");
            return;
        }
    }

    void CommandQueueContext::ExecuteCommands(RHI::HardwareQueueClass hardwareQueueClass, eastl::span<RHI::CommandList*> commandLists)
    {
        ResultCode result = GetCommandQueue(hardwareQueueClass).ExecuteCommands(commandLists);
        if (result != ResultCode::Success)
        {
            LOG_ERROR("[CommandQueueContext] Execute commands failed.");
            return;
        }
    }

    void CommandQueueContext::End()
    {
        SignalOnGpu(m_frameFences[m_currentFrameIndex]);

        // Advance to the next frame and wait for its resources to be available before continuing.
        m_currentFrameIndex = (m_currentFrameIndex + 1) % static_cast<uint32_t>(m_frameFences.size());
        {
            m_frameFences[m_currentFrameIndex].Wait();
            m_frameFences[m_currentFrameIndex].Reset();
        }
    }

    const FenceSet& CommandQueueContext::GetFlushFences()
    {
        return m_flushFences;
    }

    const FenceSet& CommandQueueContext::GetFrameFences(size_t frameIndex) const
    {
        frameIndex %= m_frameFences.size();
        return m_frameFences[frameIndex];
    }
}