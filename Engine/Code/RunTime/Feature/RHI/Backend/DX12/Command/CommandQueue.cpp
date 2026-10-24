/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "CommandQueue.h"

#include <Log/ILogSystem.h>
#include <Device/Device.h>
#include <Conversions.h>
#include <Fence/Fence.h>
#include <ID3D12Factory.h>
#include <Resource/Buffer/Buffer.h>

#include "CommandList.h"

namespace Spark::RHI::DX12
{
    RHI::ResultCode CommandQueue::InitInternal(RHI::Device& deviceBase, const RHI::CommandQueueDescriptor& descriptor)
    {
        ID3D12DeviceX* device = static_cast<Device&>(deviceBase).GetDX12Device();

        ComPtr<ID3D12CommandQueueX> queue = nullptr;
        D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
        QueueDesc.Type = ConvertHardwareQueueClass(descriptor.m_hardwareQueueClass);
        QueueDesc.NodeMask = 1;

        device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&queue));

        m_queue = queue.Get();
        m_dx12Device = device;

        return RHI::ResultCode::Success;
    }

    void CommandQueue::ShutdownInternal()
    {
        auto ID3D12Factory = Service<ID3D12FactoryInterface>::Get();
        ASSERT(ID3D12Factory, "ID3D12Factory is null!");
        ID3D12Factory->QueueForRelease(static_cast<Device&>(GetDevice()), eastl::move(m_queue));

        m_queue = nullptr;
    }

    void CommandQueue::Signal(DX12Fence& fence, uint64_t value)
    {
        fence.SetPendingValue(value);
        HRESULT hr = m_queue->Signal(fence.Get(), value);
        ASSERT(SUCCEEDED(hr), "[DX12 CommandQueue] Signal fence failed.");
    }

    void CommandQueue::Wait(DX12Fence& fence, uint64_t value)
    {
        HRESULT hr = m_queue->Wait(fence.Get(), value);
        ASSERT(SUCCEEDED(hr), "[DX12 CommandQueue] Wait fence failed.");
    }

    void CommandQueue::SignalInternal(RHI::Fence& fence, uint64_t value)
    {
        Fence& dx12Fence = static_cast<Fence&>(fence);
        Signal(dx12Fence.Get(), value);
    }

    void CommandQueue::WaitInternal(RHI::Fence& fence, uint64_t value)
    {
        Fence& dx12Fence = static_cast<Fence&>(fence);
        Wait(dx12Fence.Get(), value);
    }

    RHI::ResultCode CommandQueue::UpdateTileMappingsInternal(eastl::span<const RHI::BufferTileMapping> mappings)
    {
        if (RHI::Validation::isEnabled)
        {
            for (const RHI::BufferTileMapping& mapping : mappings)
            {
                // Only a placed buffer has a heap and an offset in it to map from.
                if (mapping.m_backingBuffer
                    && !static_cast<const Buffer*>(mapping.m_backingBuffer)->GetMemoryView().GetHeap())
                {
                    LOG_ERROR("[DX12 CommandQueue] Backing buffer {} of {} is not placed in a heap.",
                              mapping.m_backingBuffer->GetName().GetCStr(), mapping.m_buffer->GetName().GetCStr());
                    return RHI::ResultCode::InvalidArgument;
                }
            }
        }

        const uint64_t pageSize = GetDevice().GetLimits().m_reservedPageSizeInBytes;
        // One heap per call.
        for (const RHI::BufferTileMapping& mapping : mappings)
        {
            const Buffer& buffer = static_cast<const Buffer&>(*mapping.m_buffer);

            D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
            coordinate.X = static_cast<UINT>(mapping.m_byteOffset / pageSize);

            D3D12_TILE_REGION_SIZE regionSize = {};
            regionSize.NumTiles = static_cast<UINT>(mapping.m_byteCount / pageSize);
            regionSize.UseBox = FALSE;

            ID3D12Heap* heap = nullptr;
            D3D12_TILE_RANGE_FLAGS rangeFlags = D3D12_TILE_RANGE_FLAG_NULL;
            UINT heapStartTile = 0;
            if (mapping.m_backingBuffer)
            {
                const MemoryView& backingMemory = static_cast<const Buffer*>(mapping.m_backingBuffer)->GetMemoryView();
                heap = backingMemory.GetHeap();
                rangeFlags = D3D12_TILE_RANGE_FLAG_NONE;
                heapStartTile = static_cast<UINT>((backingMemory.GetHeapOffset() + mapping.m_backingByteOffset) / pageSize);
            }
            m_queue->UpdateTileMappings(
                buffer.GetMemoryView().GetMemory(),
                1, &coordinate, &regionSize,
                heap,
                1, &rangeFlags, &heapStartTile, &regionSize.NumTiles,
                D3D12_TILE_MAPPING_FLAG_NONE);
        }
        return RHI::ResultCode::Success;
    }

    void CommandQueue::WaitForIdle()
    {
        DX12Fence fence;
        fence.Init(m_dx12Device, RHI::FenceState::Reset);
        Signal(fence, fence.GetPendingValue());

        FenceEvent event("WaitForIdle");
        fence.Wait(event);
    }

    ID3D12CommandQueue* CommandQueue::GetNativeQueue() const
    {
        return m_queue.get();
    }

    void CommandQueue::ExecuteCommandsInternal(eastl::span<RHI::CommandList*> commandLists)
    {
        if (commandLists.size() == 0)
        {
            return;
        }

        eastl::vector<ID3D12CommandList*> dx12Commandlists;
        dx12Commandlists.reserve(commandLists.size());
        for (auto commandList : commandLists)
        {
            auto dx12CommandList = static_cast<CommandList*>(commandList);
            dx12Commandlists.push_back(dx12CommandList->GetCommandList());
        }

        m_queue->ExecuteCommandLists(commandLists.size(), dx12Commandlists.data());
    }
}