/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <RHI/Command/CommandQueue.h>
#include <RHI/Device/DeviceObjectFactory.h>

#include <DX12.h>

namespace Spark::RHI::DX12
{
    class Fence;
    class DX12Fence;
    class CommandLists;

    enum class HardwareQueueSubclass
    {
        Primary,
        Secondary,
    };

    struct CommandQueueDescriptor
        : public RHI::CommandQueueDescriptor
    {
        HardwareQueueSubclass m_hardwareQueueSubclass;
    };

    class CommandQueue final : public RHI::CommandQueue
    {
    public:
        //////////////////////////////////////////////////////////////////////////
        // RHI::CommandQueue
        void WaitForIdle() override;
        void ExecuteCommandsInternal(eastl::span<RHI::CommandList*> commandLists) override;
        //////////////////////////////////////////////////////////////////////////

        ID3D12CommandQueue* GetNativeQueue() const;
        void Signal(DX12Fence& fence, uint64_t value);
        void Wait(DX12Fence& fence, uint64_t value);

    private:
        friend class DeviceObjectFactory<CommandQueue>;

        CommandQueue() = default;

        //////////////////////////////////////////////////////////////////////////
        // RHI::CommandQueue
        RHI::ResultCode InitInternal(RHI::Device& device, const RHI::CommandQueueDescriptor& descriptor) override;
        void ShutdownInternal() override;
        void SignalInternal(RHI::Fence& fence, uint64_t value) override;
        void WaitInternal(RHI::Fence& fence, uint64_t value) override;
        //////////////////////////////////////////////////////////////////////////

        // void UpdateTileMappings(CommandList& commandList);

        Ptr<ID3D12CommandQueue> m_queue;
        ID3D12DeviceX* m_dx12Device;
        HardwareQueueSubclass  m_hardwareQueueSubclass{ HardwareQueueSubclass::Primary };
    };
}