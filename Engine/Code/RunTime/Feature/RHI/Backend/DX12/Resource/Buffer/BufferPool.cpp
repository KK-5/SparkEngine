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
#include <Service/Service.h>
#include <RHI/RHILimits.h>
#include <RHI/Fence/Fence.h>
#include <Device/Device.h>
#include <ID3D12Factory.h>
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

        return RHI::ResultCode::Success;
    }

    void BufferPool::ShutdownInternal()
    {
        m_releaseQueue.Shutdown();
        // Last: D3D12MA releases a pool only once all its allocations are gone.
        m_pool.reset();
    }

    RHI::ResultCode BufferPool::InitBufferInternal(RHI::Buffer& bufferBase, const RHI::BufferDescriptor& bufferDescriptor)
    {
        Buffer& buffer = static_cast<Buffer&>(bufferBase);
        if (bufferDescriptor.m_reserved)
        {
            return InitReservedBuffer(buffer, bufferDescriptor);
        }
        return InitUniqueBuffer(buffer, bufferDescriptor);
    }

    RHI::ResultCode BufferPool::InitReservedBuffer(Buffer& buffer, const RHI::BufferDescriptor& bufferDescriptor)
    {
        // D3D12MA does not create reserved resources.
        D3D12_RESOURCE_DESC resourceDesc;
        ConvertBufferDescriptor(bufferDescriptor, resourceDesc);

        ComPtr<ID3D12Resource> resource;
        HRESULT result = GetDevice().GetDX12Device()->CreateReservedResource2(
            &resourceDesc,
            D3D12_BARRIER_LAYOUT_UNDEFINED,
            nullptr,
            nullptr,
            0,
            nullptr,
            IID_PPV_ARGS(&resource)
        );
        if (FAILED(result))
        {
            LOG_ERROR("[BufferPool] Failed to create reserved buffer {} of {} bytes.",
                      buffer.GetName().GetCStr(), bufferDescriptor.m_byteCount);
            return RHI::ResultCode::Fail;
        }

        // No allocation, so no heap: what tells it from a buffer backing its pages.
        MemoryView memoryView(resource.Get(), MemoryViewType::Buffer, 0, bufferDescriptor.m_byteCount, 0);
        buffer.m_memoryView = BufferMemoryView(eastl::move(memoryView));
        return RHI::ResultCode::Success;
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
        buffer.m_memoryView = BufferMemoryView(eastl::move(memoryView));
        return RHI::ResultCode::Success;
    }

    void BufferPool::ShutdownResourceInternal(RHI::Resource& resourceBase)
    {
        Buffer& buffer = static_cast<Buffer&>(resourceBase);
        if (buffer.GetDescriptor().m_reserved)
        {
            // No D3D12MA allocation to queue: the resource itself waits out the GPU.
            Service<ID3D12FactoryInterface>::Get()->QueueForRelease(GetDevice(), buffer.GetMemoryView().GetMemory());
        }
        else
        {
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