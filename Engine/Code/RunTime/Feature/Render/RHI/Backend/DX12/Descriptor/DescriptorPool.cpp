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
#include <RHILimits.h>
#include "DescriptorFactory.h"

namespace Spark::RHI::DX12
{
    void DescriptorPool::Init(
            ID3D12DeviceX* device,
            D3D12_DESCRIPTOR_HEAP_TYPE type,
            D3D12_DESCRIPTOR_HEAP_FLAGS flags,
            uint32_t descriptorCountForHeap,
            uint32_t descriptorCountForAllocator)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc {};
        heapDesc.Type = type;
        heapDesc.Flags = flags;
        heapDesc.NumDescriptors = descriptorCountForHeap;
        heapDesc.NodeMask = 1;

        ID3D12DescriptorHeap* heap;
        device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap));
        m_descriptorHeap.Attach(heap);

        m_isGpuVisible = CheckBitsAll(flags, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
        if (m_isGpuVisible)
        {
            // It is possible for descriptorCountForAllocator to not match descriptorCountForHeap for DescriptorPoolShaderVisibleCbvSrvUav
            // heaps in which case descriptorCountForAllocator defines the number of static handles
            DescriptorTablePool::Descriptor desc;
            desc.device = device;
            desc.descriptorHeap = m_descriptorHeap.Get();
            desc.type = type;
            desc.flags = flags;
            desc.descriptorCount = descriptorCountForAllocator;
            desc.m_collectLatency = RHI::Limits::Device::FrameCountMax; // Default latency
            m_tablePool.Init(desc);
        }
        else
        {
            DescriptorHandlePool::Descriptor desc;
            desc.device = device;
            desc.descriptorHeap = m_descriptorHeap.Get();
            desc.type = type;
            desc.flags = flags;
            desc.descriptorCount = descriptorCountForAllocator;
            desc.m_collectLatency = RHI::Limits::Device::FrameCountMax; // Default latency
            m_handlePool.Init(desc);
        }
    }

    /*
    void DescriptorPool::InitPooledRange(DescriptorPool& parent, uint32_t offset, uint32_t count)
    {

    }
    */

    ID3D12DescriptorHeap* DescriptorPool::GetNativeHeap() const
    {
        return m_descriptorHeap.Get();
    }

    DescriptorHandle DescriptorPool::AllocateHandle()
    {
        if (m_isGpuVisible)
        {
            LOG_ERROR("[DescriptorPool] Trying to allocate DescriptorHandle from a GPU visible DescriptorPool. Recommanded use AllocateTable instead.");
            return DescriptorHandle();
        }
        // 不保留Ptr，因为ObjectPool内部已经管理了对象的生命周期
        return *m_handlePool.CreateObject();
    }

    void DescriptorPool::ReleaseHandle(DescriptorHandle& handle)
    {
        if (m_isGpuVisible)
        {
            LOG_ERROR("[DescriptorPool] Trying to release DescriptorHandle to a GPU visible DescriptorPool. Recommanded use ReleaseTable instead.");
            return;
        }
        m_handlePool.ShutdownObject(&handle);
    }

    DescriptorTable DescriptorPool::AllocateTable(uint32_t count)
    {
        if (!m_isGpuVisible)
        {
            LOG_ERROR("[DescriptorPool] Trying to allocate DescriptorTable from a non GPU visible DescriptorPool. Recommanded use AllocateHandle instead.");
            return DescriptorTable();
        }
        // 不保留Ptr，因为ObjectPool内部已经管理了对象的生命周期
        return *m_tablePool.CreateObject(count);
    }

    void DescriptorPool::ReleaseTable(DescriptorTable& table)
    {
        if (!m_isGpuVisible)
        {
            LOG_ERROR("[DescriptorPool] Trying to release DescriptorTable to a non GPU visible DescriptorPool. Recommanded use ReleaseHandle instead.");
            return;
        }
        m_tablePool.ShutdownObject(&table);
    }

    void DescriptorPool::Collect()
    {
        if (m_isGpuVisible)
        {
            m_tablePool.Collect();
        }
        else
        {
            m_handlePool.Collect();
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorPool::GetCpuNativeHandle(DescriptorHandle handle) const
    {
        ASSERT(handle.m_index != DescriptorHandle::NullIndex, "Index is invalid");
        return m_handlePool.GetFactory().GetD3D12CPUDescriptorHandle(handle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorPool::GetGpuNativeHandle(DescriptorHandle handle) const
    {
        ASSERT(handle.m_index != DescriptorHandle::NullIndex, "Index is invalid");
        ASSERT(handle.IsShaderVisible(), "Trying to get GPU handle from a non shader visible DescriptorHandle");
        return m_handlePool.GetFactory().GetD3D12GPUDescriptorHandle(handle);

    }

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorPool::GetCpuNativeHandleForTable(DescriptorTable table) const
    {
        DescriptorHandle handle = table.GetOffset();
        ASSERT(handle.m_index != DescriptorHandle::NullIndex, "Index is invalid");
        return m_tablePool.GetFactory().GetD3D12CPUDescriptorHandle(table);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorPool::GetGpuNativeHandleForTable(DescriptorTable table) const
    {
        DescriptorHandle handle = table.GetOffset();
        ASSERT(handle.m_index != DescriptorHandle::NullIndex, "Index is invalid");
        ASSERT(handle.IsShaderVisible(), "Trying to get GPU handle from a non shader visible DescriptorHandle");
        return m_tablePool.GetFactory().GetD3D12GPUDescriptorHandle(table);
    }
}