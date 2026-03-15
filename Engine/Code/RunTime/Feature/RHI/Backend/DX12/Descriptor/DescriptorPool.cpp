/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- using DescriptorHandleFactory/DescriptorTableFactory instead Allocator to manage DescriptorHandle/DescriptorTable
 *  -- DescriptorHandleFactory/DescriptorTableFactory is thread safe so that DescriptorPool don't need mutex anymore
 */

#include "DescriptorPool.h"

#include <Math/Bit.h>
#include <RHI/RHILimits.h>
#include "DescriptorFactory.h"

namespace Spark::RHI::DX12
{
    void DescriptorPool::Init(
            ID3D12DeviceX* device,
            D3D12_DESCRIPTOR_HEAP_TYPE type,
            D3D12_DESCRIPTOR_HEAP_FLAGS flags,
            uint32_t descriptorCountForHeap,
            uint32_t descriptorCountForPool)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc {};
        heapDesc.Type = type;
        heapDesc.Flags = flags;
        heapDesc.NumDescriptors = descriptorCountForHeap;
        heapDesc.NodeMask = 0;

        ID3D12DescriptorHeap* heap;
        device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap));
        m_descriptorHeap.Attach(heap);

        if (CheckBitsAll(flags, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
        {
            // It is possible for descriptorCountForPool to not match descriptorCountForHeap for DescriptorPoolShaderVisibleCbvSrvUav
            // heaps in which case descriptorCountForPool defines the number of static handles
            DescriptorTablePool::Descriptor desc;
            desc.device = device;
            desc.descriptorHeap = m_descriptorHeap.Get();
            desc.type = type;
            desc.flags = flags;
            desc.heapOffset = 0;
            desc.descriptorCount = descriptorCountForPool;
            desc.m_collectLatency = RHI::Limits::Device::FrameCountMax; // Default latency
            DescriptorTablePool* pool = new DescriptorTablePool;
            pool->Init(desc);
            m_internalPool.reset(pool);
        }
        else
        {
            DescriptorHandlePool::Descriptor desc;
            desc.device = device;
            desc.descriptorHeap = m_descriptorHeap.Get();
            desc.type = type;
            desc.flags = flags;
            desc.heapOffset = 0;
            desc.descriptorCount = descriptorCountForPool;
            desc.m_collectLatency = RHI::Limits::Device::FrameCountMax; // Default latency
            DescriptorHandlePool* pool = new DescriptorHandlePool;
            pool->Init(desc);
            m_internalPool.reset(pool);
        }
    }

    void DescriptorPool::InitPooledRange(ID3D12DeviceX* device, ID3D12DescriptorHeap* heap, uint32_t offset, uint32_t count)
    {
        m_descriptorHeap.Attach(heap);
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = m_descriptorHeap->GetDesc();

        DescriptorHandlePool::Descriptor desc;
        desc.device = device;
        desc.descriptorHeap = m_descriptorHeap.Get();
        desc.type = heapDesc.Type;
        desc.flags = heapDesc.Flags;
        desc.heapOffset = offset;
        desc.descriptorCount = count;
        desc.m_collectLatency = RHI::Limits::Device::FrameCountMax; // Default latency
        DescriptorHandlePool* pool = new DescriptorHandlePool;
        pool->Init(desc);
        m_internalPool.reset(pool);
    }

    ID3D12DescriptorHeap* DescriptorPool::GetNativeHeap() const
    {
        return m_descriptorHeap.Get();
    }

    DescriptorHandle DescriptorPool::AllocateHandle()
    {
        return m_internalPool->AllocateHandle();
    }

    void DescriptorPool::ReleaseHandle(DescriptorHandle& handle)
    {
        if (handle.IsNull())
        {
            return;
        }
        m_internalPool->ReleaseHandle(handle);
    }

    DescriptorTable DescriptorPool::AllocateTable(uint32_t count)
    {
        if (count == 0)
        {
            return DescriptorTable{};
        }
        return m_internalPool->AllocateTable(count);
    }

    void DescriptorPool::ReleaseTable(DescriptorTable& table)
    {
        if (table.IsNull())
        {
            return;
        }
        m_internalPool->ReleaseTable(table);
    }

    void DescriptorPool::Collect()
    {
       m_internalPool->Collect();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorPool::GetCpuNativeHandle(DescriptorHandle handle) const
    {
        ASSERT(handle.m_index != DescriptorHandle::NullIndex, "Index is invalid");
        return m_internalPool->GetCpuNativeHandle(handle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorPool::GetGpuNativeHandle(DescriptorHandle handle) const
    {
        ASSERT(handle.m_index != DescriptorHandle::NullIndex, "Index is invalid");
        ASSERT(handle.IsShaderVisible(), "Trying to get GPU handle from a non shader visible DescriptorHandle");
        return m_internalPool->GetGpuNativeHandle(handle);

    }

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorPool::GetCpuNativeHandleForTable(DescriptorTable table) const
    {
        ASSERT(table.GetOffset().m_index != DescriptorHandle::NullIndex, "Index is invalid");
        return m_internalPool->GetCpuNativeHandleForTable(table);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorPool::GetGpuNativeHandleForTable(DescriptorTable table) const
    {
        ASSERT(table.GetOffset().m_index != DescriptorHandle::NullIndex, "Index is invalid");
        ASSERT(table.GetOffset().IsShaderVisible(), "Trying to get GPU handle from a non shader visible DescriptorHandle");
        return m_internalPool->GetGpuNativeHandleForTable(table);
    }
}