/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <EASTL/span.h>

#include <RHI/Device/Device.h>

#include <Fence/Fence.h>
#include "CommandQueue.h"

namespace Spark::RHI::DX12
{
    class Device;

    class CommandQueueContext
    {
    public:
        CommandQueueContext() = default;

        void Init(RHI::Device& deviceBase);

        void Shutdown();

        CommandQueue& GetCommandQueue(RHI::HardwareQueueClass hardwareQueueClass);
        const CommandQueue& GetCommandQueue(RHI::HardwareQueueClass hardwareQueueClass) const;

        void Begin();

        // Increase the compiled fence
        uint64_t IncrementFence(RHI::HardwareQueueClass hardwareQueueClass);

        void QueueGpuSignals(FenceSet& fenceSet);

        void SignalOnGpu(FenceSet& fenceSet);

        // void ExecuteWork(RHI::HardwareQueueClass hardwareQueueClass, eastl::span<const RHI::CommandList&> commandLists);

        void WaitForIdle();

        void End();

        // Fences across all queues that are compiled by the frame graph compilation phase
        const FenceSet& GetCompiledFences();

        // Get frame fences for the specified frame
        const FenceSet& GetFrameFences(size_t frameIndex) const;

        // Get the frame index of the last executed frame
        size_t GetLastFrameIndex() const;
    
    private:
        eastl::array<Ptr<CommandQueue>, RHI::HardwareQueueClassCount> m_commandQueues;

        FenceSet m_compiledFences;
        eastl::vector<FenceSet> m_frameFences;
        uint32_t m_currentFrameIndex = 0;
        Device* m_device = nullptr;
    };
}