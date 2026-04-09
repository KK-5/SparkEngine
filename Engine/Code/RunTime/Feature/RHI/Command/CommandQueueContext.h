/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <RHI/HardwareQueue.h>
#include <RHI/Device/Device.h>

#include "CommandQueue.h"

namespace Spark::RHI
{
    class Fence;

    /**
     * A simple utility wrapping a set of fences, one for each command queue.
     */
    class FenceSet final
    {
    public:
        FenceSet() = default;

        void Init(RHI::Device& device, RHI::FenceState initialState);

        void Shutdown();

        void Wait() const;

        void Reset();

        RHI::Fence& GetFence(RHI::HardwareQueueClass hardwareQueueClass);
        const RHI::Fence& GetFence(RHI::HardwareQueueClass hardwareQueueClass) const;

    private:
        eastl::array<Ptr<Fence>, RHI::HardwareQueueClassCount> m_fences;
    };


    class CommandQueueContext final
    {
    public:
        CommandQueueContext() = default;

        RHI::ResultCode Init(RHI::Device& device);

        void Shutdown();

        CommandQueue& GetCommandQueue(RHI::HardwareQueueClass hardwareQueueClass);
        const CommandQueue& GetCommandQueue(RHI::HardwareQueueClass hardwareQueueClass) const;

        void Begin();

        // Increase the compiled fence
        void ResetCompiledFence(RHI::HardwareQueueClass hardwareQueueClass);

        /// @brief Reset compile fence and wait for complete
        void FlushCommands(RHI::HardwareQueueClass hardwareQueueClass);

        void SignalOnGpu(FenceSet& fenceSet);

        void ExecuteCommands(RHI::HardwareQueueClass hardwareQueueClass, eastl::span<RHI::CommandList*> commandLists);

        void End();

        // Fences across all queues that are compiled by the frame graph compilation phase
        const FenceSet& GetCompiledFences();

        // Get frame fences for the specified frame
        const FenceSet& GetFrameFences(size_t frameIndex) const;

    private:
        eastl::array<Ptr<CommandQueue>, RHI::HardwareQueueClassCount> m_commandQueues;

        FenceSet m_compiledFences;
        eastl::vector<FenceSet> m_frameFences;
        uint32_t m_currentFrameIndex = 0;
        Device* m_device = nullptr;
    };
}