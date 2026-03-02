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
        if (IsInitialized)
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
            ShutdownInternal();
            DeviceObject::Shutdown();
        }
    }

    void CommandQueue::QueueCommand(Command command)
    {
    
    }

    void CommandQueue::FlushCommands()
    {

    }

    void CommandQueue::ProcessQueue()
    {

    }

    void CommandQueue::ExecuteCommand(eastl::span<const CommandList&> commandLists)
    {
        if (!ValidateIsInitialized())
        {
            return;
        }

        ExecuteCommandInternal(commandLists);
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