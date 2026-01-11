/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Using DescriptorHandleFactory/DescriptorTableFactory instead Allocator to manage DescriptorHandle/DescriptorTable
 *  -- DescriptorHandleFactory/DescriptorTableFactory is thread safe so that DescriptorPool don't need mutex anymore
 */
#pragma once

#include <Object/ObjectPool.h>

#include "Descriptor.h"
#include "DescriptorFactory.h"

namespace Spark::RHI::DX12
{
    class DescriptorHandlePoolTraits : public ObjectPoolTraits
    {
    public:
        using ObjectType = DescriptorHandle;
        using ObjectFactoryType = DescriptorHandleFactory;
        using MutexType = std::mutex;
    };

    using DescriptorHandlePool = ObjectPool<DescriptorHandlePoolTraits>;

    class DescriptorTablePoolTraits : public ObjectPoolTraits
    {
    public:
        using ObjectType = DescriptorTable;
        using ObjectFactoryType = DescriptorTableFactory;
        using MutexType = std::mutex;
    };

    using DescriptorTablePool = ObjectPool<DescriptorTablePoolTraits>;

    //! This class defines a Descriptor pool which manages all the descriptors used for binding resources
    template <typename InternalPool>
    class DescriptorPool
    {
    public:
        DescriptorPool() = default;
        virtual ~DescriptorPool() = default;

        //! Initialize the native heap as well as init the allocators tracking the memory for descriptor handles
        void Init(
            ID3D12DeviceX* device,
            D3D12_DESCRIPTOR_HEAP_TYPE type,
            D3D12_DESCRIPTOR_HEAP_FLAGS flags,
            uint32_t descriptorCountForHeap,
            uint32_t descriptorCountForPool);

        //! Initialize a descriptor pool mapping a range of descriptors from a parent heap.
        void InitPooledRange(ID3D12DeviceX* device, ID3D12DescriptorHeap* heap, uint32_t offset, uint32_t count);

        ID3D12DescriptorHeap* GetNativeHeap() const;

        // Allocate和Release函数都是使用引用或者直接的值传递DescriptorHandle/DescriptorTable，因为DescriptorHandle/DescriptorTable
        // 对象的生命周期并不与资源ID3D12DescriptorHeap绑定，而是由DescriptorPool统一管理
        //! Allocate a Descriptor handles
        DescriptorHandle AllocateHandle();
        //! Release a descriptor handle
        void ReleaseHandle(DescriptorHandle& handle);
        //! Allocate a range contiguous handles (i.e Descriptor table)
        DescriptorTable AllocateTable(uint32_t count = 1);
        //! Release a range contiguous handles (i.e Descriptor table)
        void ReleaseTable(DescriptorTable& table);

        //! Garbage collection for freed handles or tables
        void Collect();
        //Get native pointers from the heap
        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandleForTable(DescriptorTable table) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuNativeHandleForTable(DescriptorTable table) const;
        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandle(DescriptorHandle handle) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuNativeHandle(DescriptorHandle handle) const;

    private:
        ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;
        InternalPool m_internalPool;
    };
    
}
