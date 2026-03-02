/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "CommandQueueContext.h"

#include <RHI/Factory.h>
#include <Device/Device.h>

#include <DX12/Device/Device.h>

namespace Spark::RHI::DX12
{
    void CommandQueueContext::Init(RHI::Device& deviceBase)
    {
        m_device = static_cast<Device*>(&deviceBase);
        m_currentFrameIndex = 0;
        m_frameFences.resize(m_device->GetDescriptor().m_frameCountMax);
        for (FenceSet& fences : m_frameFences)
        {
            fences.Init(m_device->GetDevice(), RHI::FenceState::Signaled);
        }

        m_compiledFences.Init(m_device->GetDevice(), RHI::FenceState::Reset);

        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            auto commandQueue = Service<RHI::Factory>::Get()->CreateCommandQueue();
            m_commandQueues[hardwareQueueIdx] = static_cast<CommandQueue*>(commandQueue.get());

            CommandQueueDescriptor commandQueueDesc;
            commandQueueDesc.m_hardwareQueueClass = static_cast<RHI::HardwareQueueClass>(hardwareQueueIdx);
            commandQueueDesc.m_hardwareQueueSubclass = HardwareQueueSubclass::Primary;
            m_commandQueues[hardwareQueueIdx]->Init(*m_device, commandQueueDesc);
        }
    }

    void CommandQueueContext::Shutdown()
    {
        WaitForIdle();

        m_compiledFences.Shutdown();

        for (FenceSet& fenceSet : m_frameFences)
        {
            fenceSet.Shutdown();
        }
        m_frameFences.clear();

        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            m_commandQueues[hardwareQueueIdx] = nullptr;
        }
    }

    void CommandQueueContext::SignalOnGpu(FenceSet& fenceSet)
    {
        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            const RHI::HardwareQueueClass hardwareQueueClass = static_cast<RHI::HardwareQueueClass>(hardwareQueueIdx);
            DX12Fence& fence = fenceSet.GetDX12Fence(hardwareQueueClass);
            m_commandQueues[hardwareQueueIdx]->Signal(fence);
        }
    }

    void CommandQueueContext::ExecuteWork(RHI::HardwareQueueClass hardwareQueueClass, eastl::span<const RHI::CommandList&> commandLists)
    {
        GetCommandQueue(hardwareQueueClass).ExecuteCommand(commandLists);
    }

    void CommandQueueContext::WaitForIdle()
    {
        for (uint32_t hardwareQueueIdx = 0; hardwareQueueIdx < RHI::HardwareQueueClassCount; ++hardwareQueueIdx)
        {
            if (m_commandQueues[hardwareQueueIdx])
            {
                m_commandQueues[hardwareQueueIdx]->WaitForIdle();
            }
        }
    }

    void CommandQueueContext::Begin()
    {

    }

    void CommandQueueContext::End()
    {
        SignalOnGpu(m_frameFences[m_currentFrameIndex]);

        // Advance to the next frame and wait for its resources to be available before continuing.
        m_currentFrameIndex = (m_currentFrameIndex + 1) % static_cast<uint32_t>(m_frameFences.size());
        {
            FenceEvent event("FrameFence");
            m_frameFences[m_currentFrameIndex].Wait(event);
            m_frameFences[m_currentFrameIndex].Reset();
        }
    }

    CommandQueue& CommandQueueContext::GetCommandQueue(RHI::HardwareQueueClass hardwareQueueClass)
    {
        return *m_commandQueues[static_cast<uint32_t>(hardwareQueueClass)];
    }

    const CommandQueue& CommandQueueContext::GetCommandQueue(RHI::HardwareQueueClass hardwareQueueClass) const
    {
        return *m_commandQueues[static_cast<uint32_t>(hardwareQueueClass)];
    }

    uint64_t CommandQueueContext::IncrementFence(RHI::HardwareQueueClass hardwareQueueClass)
    {
        return m_compiledFences.GetDX12Fence(hardwareQueueClass).Increment();
    }

    const FenceSet& CommandQueueContext::GetCompiledFences()
    {
        return m_compiledFences;
    }

    size_t CommandQueueContext::GetLastFrameIndex() const
    {
        return (m_currentFrameIndex + m_frameFences.size() - 1) % m_frameFences.size();
    }

    const FenceSet& CommandQueueContext::GetFrameFences(size_t frameIndex) const
    {
        frameIndex %= m_frameFences.size();
        return m_frameFences[frameIndex];
    }
}