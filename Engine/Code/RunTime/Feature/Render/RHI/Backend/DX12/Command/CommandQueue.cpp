/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "CommandQueue.h"

#include <Device/Device.h>
#include <Conversions.h>
#include <Fence/Fence.h>

#include "CommandList.h"

namespace Spark::RHI::DX12
{
    RHI::ResultCode CommandQueue::InitInternal(RHI::Device& deviceBase, const RHI::CommandQueueDescriptor& descriptor)
    {
        ID3D12DeviceX* device = static_cast<Device&>(deviceBase).GetDevice();

        ComPtr<ID3D12CommandQueueX> queue = nullptr;
        D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
        QueueDesc.Type = ConvertHardwareQueueClass(descriptor.m_hardwareQueueClass);
        QueueDesc.NodeMask = 1;

        device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&queue));

        m_queue = queue.Get();
        m_dx12Device = device;
    }

    void CommandQueue::ShutdownInternal()
    {
        if (m_queue)
        {
            m_queue.reset();
        }

    }

    void CommandQueue::Signal(DX12Fence& fence)
    {
        m_queue->Signal(fence.Get(), fence.GetPendingValue());
    }

    void CommandQueue::ExecuteWork(const RHI::ExecuteWorkRequest& request)
    {

    }

    void CommandQueue::WaitForIdle()
    {
        DX12Fence fence;
        fence.Init(m_dx12Device, RHI::FenceState::Reset);
        Signal(fence);

        FenceEvent event("WaitForIdle");
        fence.Wait(event);
    }

    ID3D12CommandQueue* CommandQueue::GetNativeQueue() const
    {
        m_queue.get();
    }

    void CommandQueue::ExecuteCommandInternal(eastl::span<const RHI::CommandList> commandLists)
    {
        if (commandLists.size() == 0)
        {
            return;
        }

        eastl::vector<ID3D12CommandList*> dx12Commandlists;
        dx12Commandlists.reserve(commandLists.size());
        for (const auto commandList : commandLists)
        {
            auto dx12CommandList = static_cast<const CommandList>(commandList);
            dx12Commandlists.push_back(dx12CommandList.GetCommandList());
        }

        m_queue->ExecuteCommandLists(commandLists.size(), dx12Commandlists.data());
    }
}