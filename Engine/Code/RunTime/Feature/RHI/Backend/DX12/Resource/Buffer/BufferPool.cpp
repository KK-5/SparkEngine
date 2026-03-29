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
#include <Math/Bit.h>
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
        ResourcePool::OnFrameEnd();
    }

    RHI::ResultCode BufferPool::InitInternal(RHI::Device& deviceBase, const RHI::BufferPoolDescriptor& descriptorBase)
    {
        Device& device = static_cast<Device&>(deviceBase);

        uint32_t bufferPageSize = RHI::DefaultValues::Memory::BufferPoolPageSizeInBytes;

        if (descriptorBase.m_largestPooledAllocationSizeInBytes > 0)
        {
            bufferPageSize = eastl::max<uint32_t>(bufferPageSize, static_cast<uint32_t>(descriptorBase.m_largestPooledAllocationSizeInBytes));
        }

        D3D12MA::ALLOCATOR_DESC desc = {};
        desc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;
        desc.pDevice = device.GetDX12Device();
        desc.pAdapter = device.GetPhysicalDevice().GetAdapter();
        desc.PreferredBlockSize = bufferPageSize;

        D3D12MA::Allocator* pAllocator;
        if (FAILED(D3D12MA::CreateAllocator(&desc, &pAllocator)))
        {
            LOG_ERROR("[BufferPool] Failed to initialize the D3D12MemoryAllocator.");
            return RHI::ResultCode::Fail;
        }
        m_allocator = pAllocator;

        D3D12MAReleaseQueue::Descriptor releaseQueueDescriptor;
        releaseQueueDescriptor.m_collectLatency = device.GetDescriptor().m_frameCountMax;
        m_releaseQueue.Init(releaseQueueDescriptor);

        return RHI::ResultCode::Success;
    }

    void BufferPool::ShutdownInternal()
    {
        m_allocator.reset();
    }

    RHI::ResultCode BufferPool::InitBufferInternal(RHI::Buffer& bufferBase, const RHI::BufferDescriptor& bufferDescriptor)
    {
        D3D12_RESOURCE_DESC resourceDesc;
        ConvertBufferDescriptor(bufferDescriptor, resourceDesc);

        D3D12MA::ALLOCATION_DESC allocDesc = {};
        allocDesc.HeapType = ConvertHeapType(GetDescriptor().m_heapMemoryLevel, GetDescriptor().m_hostMemoryAccess);
        allocDesc.Flags = D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_STRATEGY_BEST_FIT;

        // Derive D3D12 initial state from pool descriptor directly.
        // Upload heap → GENERIC_READ (fixed), Readback → COPY_DEST (fixed), Device → COMMON.
        D3D12_RESOURCE_STATES initialResourceState;
        if (GetDescriptor().m_heapMemoryLevel == RHI::HeapMemoryLevel::Host)
        {
            initialResourceState = (GetDescriptor().m_hostMemoryAccess == RHI::HostMemoryAccess::Write)
                ? D3D12_RESOURCE_STATE_GENERIC_READ
                : D3D12_RESOURCE_STATE_COPY_DEST;
        }
        else
        {
            initialResourceState = CheckBitsAny(bufferDescriptor.m_bindFlags, RHI::BufferBindFlags::RayTracingAccelerationStructure)
                ? D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
                : D3D12_RESOURCE_STATE_COMMON;
        }

        D3D12MA::Allocation* allocation = nullptr;
        HRESULT result = m_allocator->CreateResource(
            &allocDesc,
            &resourceDesc,
            initialResourceState,
            NULL,
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
        MemoryView memoryView(allocation, MemoryViewType::Buffer, 0, bufferDescriptor.m_byteCount, bufferDescriptor.m_alignment);
        BufferMemoryView bufferMemoryView(eastl::move(memoryView), allocation->GetHeap() ? BufferMemoryType::Shared : BufferMemoryType::Unique);
        Buffer& buffer = static_cast<Buffer&>(bufferBase);
        buffer.m_memoryView = eastl::move(bufferMemoryView);
        return RHI::ResultCode::Success;
    }

    void BufferPool::ShutdownResourceInternal(RHI::Resource& resourceBase)
    {
        Buffer& buffer = static_cast<Buffer&>(resourceBase);
        m_releaseQueue.Collect(buffer.GetMemoryView().GetMemoryAllocation());
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

    RHI::ResultCode BufferPool::StreamBufferInternal(const RHI::BufferStreamRequest& request)
    {
        // [TODO]
        return RHI::ResultCode::InvalidOperation;
    }
}