/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <RHI/Command/CommandQueue.h>

#include <DX12.h>

namespace Spark::RHI::DX12
{
    class Fence;
    class DX12Fence;

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
        void ExecuteWork(const RHI::ExecuteWorkRequest& request) override;
        void WaitForIdle() override;
        void ExecuteCommandInternal(eastl::span<const RHI::CommandList&> commandLists) override;
        //////////////////////////////////////////////////////////////////////////

        void Singal(DX12Fence& fence);
        ID3D12CommandQueue* GetNativeQueue() const;
    
    private:
        CommandQueue() = default;

        //////////////////////////////////////////////////////////////////////////
        // RHI::CommandQueue
        RHI::ResultCode InitInternal(RHI::Device& device, const RHI::CommandQueueDescriptor& descriptor) override;
        void ShutdownInternal() override;
        //////////////////////////////////////////////////////////////////////////

        // void UpdateTileMappings(CommandList& commandList);

        Ptr<ID3D12CommandQueue> m_queue;
        ID3D12DeviceX* m_dx12Device;
        HardwareQueueSubclass  m_hardwareQueueSubclass{ HardwareQueueSubclass::Primary };
    };
}