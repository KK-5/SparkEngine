/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/span.h>

#include <RHI/RHILimits.h>
#include <RHI/HardwareQueue.h>
#include <RHI/Device/DeviceObject.h>
#include <RHI/Fence/Fence.h>

namespace Spark::RHI
{
    class CommandList;

    struct ExecuteWorkRequest
    {

    };

    struct CommandQueueDescriptor
    {
        HardwareQueueClass m_hardwareQueueClass = HardwareQueueClass::Graphics;
        int m_maxFrameQueueDepth = Limits::Device::FrameCountMax;
    };

    //! Base class that provides the backend API the ability to add
    //! commands to a queue which are executed on a separate thread
    class CommandQueue : public DeviceObject
    {
    public:
        CommandQueue() = default;
        virtual ~CommandQueue() = default;
        ResultCode Init(Device& device, const CommandQueueDescriptor& descriptor);
        void Shutdown();

        ResultCode FlushCommands(RHI::Fence& fence);
        ResultCode ExecuteCommands(eastl::span<CommandList*> commandLists);
        ResultCode Signal(RHI::Fence& fence);

        virtual void WaitForIdle() = 0;
            
        RHI::HardwareQueueClass GetHardwareQueueClass() const;
        const CommandQueueDescriptor& GetDescriptor() const;

    protected:
        //////////////////////////////////////////////////////////////////////////
        // Functions that must be implemented by each RHI.
        virtual ResultCode InitInternal(Device& device, const CommandQueueDescriptor& descriptor) = 0;
        virtual void ExecuteWork(const ExecuteWorkRequest& request) = 0;
        virtual void ExecuteCommandsInternal(eastl::span<CommandList*> commandLists) = 0;
        virtual void SignalInternal(RHI::Fence& fence) = 0;
        virtual void ShutdownInternal() = 0;
        //////////////////////////////////////////////////////////////////////////

    private:
        bool ValidateIsInitialized() const;

        void ProcessQueue();
        CommandQueueDescriptor m_descriptor;

    };
}