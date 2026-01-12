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
    template <typename InternalPool>
    void DescriptorPool<InternalPool>::Init(
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

        // It is possible for descriptorCountForPool to not match descriptorCountForHeap for DescriptorPoolShaderVisibleCbvSrvUav
        // heaps in which case descriptorCountForPool defines the number of static handles
        InternalPool::Descriptor desc;
        desc.device = device;
        desc.descriptorHeap = m_descriptorHeap.Get();
        desc.type = type;
        desc.flags = flags;
        desc.heapOffset = 0;
        desc.descriptorCount = descriptorCountForPool;
        desc.m_collectLatency = RHI::Limits::Device::FrameCountMax; // Default latency
        m_internalPool.Init(desc);
    }

    template <typename InternalPool>
    void DescriptorPool<InternalPool>::InitPooledRange(ID3D12DeviceX* device, ID3D12DescriptorHeap* heap, uint32_t offset, uint32_t count)
    {
        m_descriptorHeap.Attach(parent.GetNativeHeap());
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = m_descriptorHeap->GetDesc();

        InternalPool::Descriptor desc;
        desc.device = device;
        desc.descriptorHeap = m_descriptorHeap.Get();
        desc.type = heapDesc.Type;
        desc.flags = heapDesc.Flags;
        desc.heapOffset = offset;
        desc.descriptorCount = count;
        desc.m_collectLatency = RHI::Limits::Device::FrameCountMax; // Default latency
        m_internalPool.Init(desc);
    }

    template <typename InternalPool>
    ID3D12DescriptorHeap* DescriptorPool<InternalPool>::GetNativeHeap() const
    {
        return m_descriptorHeap.Get();
    }

    template <typename InternalPool>
    DescriptorHandle DescriptorPool<InternalPool>::AllocateHandle()
    {
        /*
        if (m_isGpuVisible)
        {
            LOG_ERROR("[DescriptorPool] Trying to allocate DescriptorHandle from a GPU visible DescriptorPool. Recommanded use AllocateTable instead.");
            return DescriptorHandle();
        }
        */
        // 不保留Ptr，因为ObjectPool内部已经管理了对象的生命周期
        return *m_internalPool.CreateObject();
    }

    template <typename InternalPool>
    void DescriptorPool<InternalPool>::ReleaseHandle(DescriptorHandle& handle)
    {
        /*
        if (m_isGpuVisible)
        {
            LOG_ERROR("[DescriptorPool] Trying to release DescriptorHandle to a GPU visible DescriptorPool. Recommanded use ReleaseTable instead.");
            return;
        }
        */
        if (handle.IsNull)
        {
            return;
        }
        m_internalPool.ShutdownObject(&handle);
    }

    template <typename InternalPool>
    DescriptorTable DescriptorPool<InternalPool>::AllocateTable(uint32_t count)
    {
        /*
        if (!m_isGpuVisible)
        {
            LOG_ERROR("[DescriptorPool] Trying to allocate DescriptorTable from a non GPU visible DescriptorPool. Recommanded use AllocateHandle instead.");
            return DescriptorTable();
        }
        */
        // 不保留Ptr，因为ObjectPool内部已经管理了对象的生命周期
        return *m_internalPool.CreateObject(count);
    }

    template <typename InternalPool>
    void DescriptorPool<InternalPool>::ReleaseTable(DescriptorTable& table)
    {
        /*
        if (!m_isGpuVisible)
        {
            LOG_ERROR("[DescriptorPool] Trying to release DescriptorTable to a non GPU visible DescriptorPool. Recommanded use ReleaseHandle instead.");
            return;
        }
        */
        if (table.IsNull())
        {
            return;
        }
        m_internalPool.ShutdownObject(&table);
    }

    template <typename InternalPool>
    void DescriptorPool<InternalPool>::Collect()
    {
       m_internalPool.Collect();
    }

    template <typename InternalPool>
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorPool<InternalPool>::GetCpuNativeHandle(DescriptorHandle handle) const
    {
        ASSERT(handle.m_index != DescriptorHandle::NullIndex, "Index is invalid");
        return m_internalPool.GetFactory().GetD3D12CPUDescriptorHandle(handle);
    }

    template <typename InternalPool>
    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorPool<InternalPool>::GetGpuNativeHandle(DescriptorHandle handle) const
    {
        ASSERT(handle.m_index != DescriptorHandle::NullIndex, "Index is invalid");
        ASSERT(handle.IsShaderVisible(), "Trying to get GPU handle from a non shader visible DescriptorHandle");
        return m_internalPool.GetFactory().GetD3D12GPUDescriptorHandle(handle);

    }

    template <typename InternalPool>
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorPool<InternalPool>::GetCpuNativeHandleForTable(DescriptorTable table) const
    {
        DescriptorHandle handle = table.GetOffset();
        ASSERT(handle.m_index != DescriptorHandle::NullIndex, "Index is invalid");
        return m_internalPool.GetFactory().GetD3D12CPUDescriptorHandle(table);
    }

    template <typename InternalPool>
    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorPool<InternalPool>::GetGpuNativeHandleForTable(DescriptorTable table) const
    {
        DescriptorHandle handle = table.GetOffset();
        ASSERT(handle.m_index != DescriptorHandle::NullIndex, "Index is invalid");
        ASSERT(handle.IsShaderVisible(), "Trying to get GPU handle from a non shader visible DescriptorHandle");
        return m_internalPool.GetFactory().GetD3D12GPUDescriptorHandle(table);
    }

    // 显式实例化两个模板类
    template class DescriptorPool<DescriptorHandlePool>;
    template class DescriptorPool<DescriptorTablePool>;
}