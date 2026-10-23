/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Remove BufferPoolResolver (staging buffer auto-allocation for Device heap).
 *  -- Device heap buffers now reject Map, matching native D3D12 behavior.
 */

#include "BufferPool.h"

#include <mutex>
#include <EASTL/vector.h>
#include <Math/Bit.h>
#include <Service/Service.h>
#include <RHI/Factory.h>
#include <RHI/RHILimits.h>
#include <RHI/Fence/Fence.h>
#include <Device/Device.h>
#include <Memory.h>
#include <MemoryView.h>
#include <Conversions.h>

#include "Buffer.h"

namespace Spark::RHI::DX12
{
    Device& BufferPool::GetDevice() const
    {
        return static_cast<Device&>(DeviceObject::GetDevice());
    }

    void BufferPool::OnFrameEnd()
    {
        m_releaseQueue.Collect();
        m_virtualBlockReleaseQueue.Collect();
        ResourcePool::OnFrameEnd();
    }

    RHI::ResultCode BufferPool::InitInternal(RHI::Device& deviceBase, const RHI::BufferPoolDescriptor& descriptorBase)
    {
        Device& device = static_cast<Device&>(deviceBase);

        D3D12MA::POOL_DESC poolDesc = {};
        poolDesc.Flags                  = D3D12MA::POOL_FLAG_NONE;
        poolDesc.HeapProperties.Type    = ConvertHeapType(descriptorBase.m_heapMemoryLevel, descriptorBase.m_hostMemoryAccess);
        poolDesc.HeapFlags              = D3D12_HEAP_FLAG_NONE;
        poolDesc.BlockSize              = descriptorBase.m_blockSizeInBytes;
        poolDesc.MinAllocationAlignment = descriptorBase.m_minAllocationAlignment;

        ComPtr<D3D12MA::Pool> pool;
        if (FAILED(device.GetAllocator()->CreatePool(&poolDesc, &pool)))
        {
            LOG_ERROR("[BufferPool] Failed to create the D3D12MA pool of buffer pool {}.", GetName().GetCStr());
            return RHI::ResultCode::Fail;
        }
        m_pool = pool.Get();

        D3D12MAReleaseQueue::Descriptor releaseQueueDescriptor;
        releaseQueueDescriptor.m_collectLatency = device.GetDescriptor().m_frameCountMax;
        m_releaseQueue.Init(releaseQueueDescriptor);

        if (descriptorBase.m_budgetInBytes != 0)
        {
            VirtualBlockAllocationReleaseQueue::Descriptor virtualBlockReleaseQueueDescriptor;
            virtualBlockReleaseQueueDescriptor.m_collectLatency = device.GetDescriptor().m_frameCountMax;
            m_virtualBlockReleaseQueue.Init(virtualBlockReleaseQueueDescriptor);

            return InitBaseBuffer(device, descriptorBase);
        }

        return RHI::ResultCode::Success;
    }

    RHI::ResultCode BufferPool::InitBaseBuffer(Device& device, const RHI::BufferPoolDescriptor& descriptor)
    {
        // Every buffer of the pool is used through this one, so it takes all the pool allows.
        RHI::BufferDescriptor bufferDescriptor;
        bufferDescriptor.m_byteCount = descriptor.m_budgetInBytes;
        bufferDescriptor.m_bindFlags = descriptor.m_bindFlags;

        D3D12_RESOURCE_DESC resourceDesc;
        ConvertBufferDescriptor(bufferDescriptor, resourceDesc);
        const D3D12_RESOURCE_DESC1 resourceDesc1 = ConvertResourceDesc1(resourceDesc);

        D3D12MA::ALLOCATION_DESC allocDesc = {};
        allocDesc.CustomPool = m_pool.get();
        // Nothing is placed beside it.
        allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;

        ComPtr<D3D12MA::Allocation> allocation = nullptr;
        HRESULT result = device.GetAllocator()->CreateResource3(
            &allocDesc,
            &resourceDesc1,
            D3D12_BARRIER_LAYOUT_UNDEFINED,
            NULL,
            0,
            nullptr,
            &allocation,
            IID_NULL,
            NULL
        );
        if (FAILED(result))
        {
            LOG_ERROR("[BufferPool] Failed to create the base buffer of {} bytes.", descriptor.m_budgetInBytes);
            return RHI::ResultCode::Fail;
        }

        // In bytes. The resource may be larger (ConvertBufferDescriptor rounds up); the rest
        // is left unused.
        D3D12MA::VIRTUAL_BLOCK_DESC blockDesc = {};
        blockDesc.Size = descriptor.m_budgetInBytes;

        ComPtr<D3D12MA::VirtualBlock> virtualBlock = nullptr;
        if (FAILED(D3D12MA::CreateVirtualBlock(&blockDesc, &virtualBlock)))
        {
            LOG_ERROR("[BufferPool] CreateVirtualBlock failed.");
            return RHI::ResultCode::Fail;
        }

        // The whole native buffer as a buffer of the pool. It owns the memory its parts are in.
        Ptr<RHI::Buffer> baseBuffer = Service<RHI::Factory>::Get()->CreateBuffer();
        baseBuffer->SetName(GetName());
        MemoryView memoryView(allocation.Get(), MemoryViewType::Buffer, 0, descriptor.m_budgetInBytes, 0);
        static_cast<Buffer&>(*baseBuffer).m_memoryView = BufferMemoryView(eastl::move(memoryView), BufferMemoryType::Unique);
        m_baseBuffer = eastl::move(baseBuffer);

        m_virtualBlock = virtualBlock.Get();
        return RHI::ResultCode::Success;
    }

    RHI::Buffer* BufferPool::GetBaseBuffer() const
    {
        return m_baseBuffer.get();
    }

    void BufferPool::ShutdownInternal()
    {
        // Before the queue its memory waits in is emptied.
        m_baseBuffer.reset();
        m_releaseQueue.Shutdown();
        // Each part holds the block, which goes with the last of them.
        m_virtualBlockReleaseQueue.Shutdown();
        m_virtualBlock.reset();
        // Last: D3D12MA releases a pool only once all its allocations are gone.
        m_pool.reset();
    }

    RHI::ResultCode BufferPool::InitSubAllocatedBuffer(Buffer& buffer, const RHI::BufferDescriptor& bufferDescriptor)
    {
        // VirtualBlock aligns to powers of two only. Any other alignment takes up to one
        // alignment more and starts at the first aligned byte of what it got.
        const uint64_t alignment    = eastl::max<uint64_t>(bufferDescriptor.m_alignment, 1);
        const bool     isPowerOfTwo = IsPowerOfTwo(alignment);

        D3D12MA::VIRTUAL_ALLOCATION_DESC allocDesc = {};
        allocDesc.Size      = isPowerOfTwo ? bufferDescriptor.m_byteCount : bufferDescriptor.m_byteCount + alignment - 1;
        allocDesc.Alignment = isPowerOfTwo ? alignment : 1;

        D3D12MA::VirtualAllocation allocation;
        UINT64 offset = 0;
        if (FAILED(m_virtualBlock->Allocate(&allocDesc, &allocation, &offset)))
        {
            // Free bytes below the request: too small a budget. Above it: fragmented.
            D3D12MA::DetailedStatistics statistics = {};
            m_virtualBlock->CalculateStatistics(&statistics);
            LOG_ERROR("[BufferPool] Pool {} has no room for buffer {} ({} bytes, alignment {}): {} of its {} bytes "
                      "are free, the largest free range is {} bytes.",
                GetName().GetCStr(), buffer.GetName().GetCStr(), bufferDescriptor.m_byteCount, alignment,
                GetDescriptor().m_budgetInBytes - statistics.Stats.AllocationBytes, GetDescriptor().m_budgetInBytes,
                statistics.UnusedRangeSizeMax);
            return RHI::ResultCode::OutOfMemory;
        }
        offset = AlignUpNPOT(offset, alignment);

        Memory* baseMemory = static_cast<const Buffer&>(*m_baseBuffer).GetMemoryView().GetMemory();
        MemoryView memoryView(baseMemory, MemoryViewType::Buffer, offset, bufferDescriptor.m_byteCount, alignment);
        buffer.m_memoryView = BufferMemoryView(
            eastl::move(memoryView),
            Ptr<VirtualBlockAllocation>(new VirtualBlockAllocation(m_virtualBlock.get(), allocation)));
        return RHI::ResultCode::Success;
    }

    RHI::ResultCode BufferPool::InitBufferInternal(RHI::Buffer& bufferBase, const RHI::BufferDescriptor& bufferDescriptor)
    {
        Buffer& buffer = static_cast<Buffer&>(bufferBase);
        return m_virtualBlock
            ? InitSubAllocatedBuffer(buffer, bufferDescriptor)
            : InitUniqueBuffer(buffer, bufferDescriptor);
    }

    RHI::ResultCode BufferPool::InitUniqueBuffer(Buffer& buffer, const RHI::BufferDescriptor& bufferDescriptor)
    {
        D3D12_RESOURCE_DESC resourceDesc;
        ConvertBufferDescriptor(bufferDescriptor, resourceDesc);

        D3D12MA::ALLOCATION_DESC allocDesc = {};
        allocDesc.CustomPool = m_pool.get();
        allocDesc.Flags = D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_STRATEGY_BEST_FIT;

        // Buffers have no layout; upload / readback heaps included.
        const D3D12_RESOURCE_DESC1 resourceDesc1 = ConvertResourceDesc1(resourceDesc);
        ComPtr<D3D12MA::Allocation> allocation = nullptr;
        HRESULT result = GetDevice().GetAllocator()->CreateResource3(
            &allocDesc,
            &resourceDesc1,
            D3D12_BARRIER_LAYOUT_UNDEFINED,
            NULL,
            0,
            nullptr,
            &allocation,
            IID_NULL,
            NULL
        );

        if (FAILED(result))
        {
            LOG_ERROR("[BufferPool] D3D12MA Create buffer resource failed!");
            return RHI::ResultCode::Fail;
        }

        // 创建一个默认BufferMemoryView，使用全部Memory(ID3DResource)
        MemoryView memoryView(allocation.Get(), MemoryViewType::Buffer, 0, bufferDescriptor.m_byteCount, bufferDescriptor.m_alignment);
        buffer.m_memoryView = BufferMemoryView(eastl::move(memoryView), BufferMemoryType::Unique);
        return RHI::ResultCode::Success;
    }

    void BufferPool::ShutdownResourceInternal(RHI::Resource& resourceBase)
    {
        Buffer& buffer = static_cast<Buffer&>(resourceBase);
        if (VirtualBlockAllocation* part = buffer.GetMemoryView().GetVirtualBlockAllocation())
        {
            m_virtualBlockReleaseQueue.QueueForCollect(part);
        }
        else
        {
            // A buffer of its own, or the base buffer.
            m_releaseQueue.QueueForCollect(buffer.GetMemoryView().GetMemoryAllocation());
        }
        // 这里移动赋值，原MemoryView持有的MemoryAllocation自动release
        buffer.m_memoryView = {};
        buffer.m_pendingResolves = 0;
    }

    RHI::ResultCode BufferPool::OrphanBufferInternal(RHI::Buffer& bufferBase)
    {
        // [TODO]
        return RHI::ResultCode::InvalidOperation;
    }

    RHI::ResultCode BufferPool::MapBufferInternal(const RHI::BufferMapRequest& request, RHI::BufferMapResponse& response)
    {
        const RHI::BufferPoolDescriptor& poolDescriptor = GetDescriptor();
        Buffer& buffer = *static_cast<Buffer*>(request.m_buffer);
        CpuVirtualAddress mappedData = nullptr;

        if (poolDescriptor.m_heapMemoryLevel == RHI::HeapMemoryLevel::Host)
        {
            mappedData = buffer.GetMemoryView().Map(poolDescriptor.m_hostMemoryAccess);

            if (!mappedData)
            {
                return RHI::ResultCode::Fail;
            }
            mappedData += request.m_byteOffset;
        }
        else
        {
            // Device heap buffers should never reach here — blocked by RHI::BufferPool::MapBuffer.
            return RHI::ResultCode::InvalidOperation;
        }

        response.m_data = mappedData;
        return RHI::ResultCode::Success;
    }

    void BufferPool::UnmapBufferInternal(RHI::Buffer& bufferBase)
    {
        const RHI::BufferPoolDescriptor& poolDescriptor = GetDescriptor();
        Buffer& buffer = static_cast<Buffer&>(bufferBase);

        if (poolDescriptor.m_heapMemoryLevel == RHI::HeapMemoryLevel::Host)
        {
            buffer.GetMemoryView().Unmap(poolDescriptor.m_hostMemoryAccess);
        }
    }

}