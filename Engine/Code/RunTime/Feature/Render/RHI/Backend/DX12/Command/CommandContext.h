/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <RHI/Device/Device.h>

#include <Fence/Fence.h>
#include "CommandQueue.h"

namespace Spark::RHI::DX12
{
    class CommandQueueContext
    {
    public:
        CommandQueueContext() = default;

        void Init(RHI::Device& deviceBase);

        void Shutdown();

        CommandQueue& GetCommandQueue(RHI::HardwareQueueClass hardwareQueueClass);
        const CommandQueue& GetCommandQueue(RHI::HardwareQueueClass hardwareQueueClass) const;

        void Begin();

        uint64_t IncrementFence(RHI::HardwareQueueClass hardwareQueueClass);

        void QueueGpuSignals(FenceSet& fenceSet);

        void WaitForIdle();

        void End();

    };
}