/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "CommandQueue.h"

#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    bool CommandQueue::ValidateIsInitialized() const
    {
        if (Validation::isEnabled)
        {
            if (!IsInitialized())
            {
                LOG_ERROR("[CommandQueue] CommandQueue is not initialized!");
                return false;
            }
        }

        return true;
    }

    ResultCode CommandQueue::Init(Device& device, const CommandQueueDescriptor& descriptor)
    {
        if (IsInitialized())
        {
            LOG_ERROR("[CommandQueue] CommandQueue is already initialized!");
            return ResultCode::InvalidOperation;
        }

        const ResultCode resultCode = InitInternal(device, descriptor);

        if (resultCode == ResultCode::Success)
        {
            DeviceObject::Init(device);
                
            m_descriptor = descriptor;
        }
        return resultCode;
    }

    void CommandQueue::Shutdown()
    {
        if (ValidateIsInitialized())
        {
            WaitForIdle();
            ShutdownInternal();
            DeviceObject::Shutdown();
        }
    }

    ResultCode CommandQueue::FlushCommands(RHI::Fence& fence)
    {
        if (fence.Reset() != RHI::ResultCode::Success)
        {
            LOG_ERROR("[CommandQueue] Reset Fence failed.");
            return ResultCode::Fail;
        }

        if (Signal(fence) != ResultCode::Success)
        {
            LOG_ERROR("[CommandQueue] Signal Fence failed.");
            return ResultCode::Fail;
        }
            
        fence.WaitOnCpu();
        return ResultCode::Success;
    }

    ResultCode CommandQueue::ExecuteCommands(eastl::span<CommandList*> commandLists)
    {
        if (!ValidateIsInitialized())
        {
            LOG_ERROR("[CommandQueue] CommandQueue is not initialize.");
            return ResultCode::InvalidOperation;
        }

        ExecuteCommandsInternal(commandLists);
        return ResultCode::Success;
    }

    ResultCode CommandQueue::Signal(RHI::Fence& fence, uint64_t value)
    {
        if (!ValidateIsInitialized())
        {
            LOG_ERROR("[CommandQueue] CommandQueue is not initialize.");
            return ResultCode::InvalidOperation;
        }

        if (!fence.IsInitialized())
        {
            LOG_ERROR("[CommandQueue] Fence is not initialize.");
            return ResultCode::InvalidOperation;
        }

        fence.SetPendingValue(value);
        SignalInternal(fence, value);

        return ResultCode::Success;
    }

    ResultCode CommandQueue::Signal(RHI::Fence& fence)
    {
        return Signal(fence, fence.GetPendingValue());
    }

    ResultCode CommandQueue::Wait(RHI::Fence& fence, uint64_t value)
    {
        if (!ValidateIsInitialized())
        {
            LOG_ERROR("[CommandQueue] CommandQueue is not initialize.");
            return ResultCode::InvalidOperation;
        }

        if (!fence.IsInitialized())
        {
            LOG_ERROR("[CommandQueue] Fence is not initialize.");
            return ResultCode::InvalidOperation;
        }

        WaitInternal(fence, value);

        return ResultCode::Success;
    }

    RHI::HardwareQueueClass CommandQueue::GetHardwareQueueClass() const
    {
        return m_descriptor.m_hardwareQueueClass;
    }

    const CommandQueueDescriptor& CommandQueue::GetDescriptor() const
    {
        return m_descriptor;
    }
}