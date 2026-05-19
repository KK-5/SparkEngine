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
 *  -- Collect() must not run concurrently with Allocate() / DeAllocate(); external synchronization is required.
 */
#pragma once

#include <EASTL/unique_ptr.h>
#include <Object/ObjectPool.h>
#include <Log/SpdLogSystem.h>
#include <RHI/RHILimits.h>

#include "Descriptor.h"
#include "DescriptorFactory.h"

namespace Spark::RHI::DX12
{
    class DescriptorPoolBase
    {
    public:
        virtual ~DescriptorPoolBase() = default;

        virtual DescriptorHandle AllocateHandle() = 0;
        virtual void ReleaseHandle(DescriptorHandle& handle) = 0;
        virtual DescriptorTable AllocateTable(uint32_t count = 1) = 0;
        virtual void ReleaseTable(DescriptorTable& table) = 0;

        virtual D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandleForTable(DescriptorTable table) const = 0;
        virtual D3D12_GPU_DESCRIPTOR_HANDLE GetGpuNativeHandleForTable(DescriptorTable table) const = 0;
        virtual D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandle(DescriptorHandle handle) const = 0;
        virtual D3D12_GPU_DESCRIPTOR_HANDLE GetGpuNativeHandle(DescriptorHandle handle) const = 0;

        virtual void Collect() = 0;
    };

    class DescriptorHandlePoolTraits : public ObjectPoolTraits
    {
    public:
        using ObjectType = DescriptorHandle;
        using ObjectFactoryType = DescriptorHandleFactory;
        using MutexType = std::mutex;
        using StorageType = DescriptorHandle*;
    };

    class DescriptorHandlePool final : public ObjectPool<DescriptorHandlePoolTraits>,
                                       public DescriptorPoolBase
    {
        using BasePool = ObjectPool<DescriptorHandlePoolTraits>;
    public:
        DescriptorHandle AllocateHandle() override
        {
            return *BasePool::Allocate();
        }

        void ReleaseHandle(DescriptorHandle& handle) override
        {
            BasePool::DeAllocate(&handle);
        }

        DescriptorTable AllocateTable(uint32_t count = 1) override
        {
            ASSERT(count == 1, "DescriptorHandlePool just support allocate single handle.");
            return DescriptorTable(AllocateHandle(), 1);
        }

        void ReleaseTable(DescriptorTable& table) override
        {
            ASSERT(table.GetSize() == 1, "Try to release a desciriptortable that is not allocate from this pool");
            DescriptorHandle handle = table.GetOffset();
            ReleaseHandle(handle);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandleForTable(DescriptorTable table) const override
        {
            ASSERT(table.GetSize() == 1, "Try to release a desciriptortable that is not allocate from this pool");
            return BasePool::GetFactory().GetD3D12CPUDescriptorHandle(table.GetOffset());
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuNativeHandleForTable(DescriptorTable table) const override
        {
            ASSERT(table.GetSize() == 1, "Try to release a desciriptortable that is not allocate from this pool");
            return BasePool::GetFactory().GetD3D12GPUDescriptorHandle(table.GetOffset());
        }

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandle(DescriptorHandle handle) const override
        {
            return BasePool::GetFactory().GetD3D12CPUDescriptorHandle(handle);
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuNativeHandle(DescriptorHandle handle) const override
        {
            return BasePool::GetFactory().GetD3D12GPUDescriptorHandle(handle);
        }

        void Collect() override
        {
            BasePool::Collect();
        }
    };

    class DescriptorTablePoolTraits : public ObjectPoolTraits
    {
    public:
        using ObjectType = DescriptorTable;
        using ObjectFactoryType = DescriptorTableFactory;
        using MutexType = std::mutex;
        using StorageType = DescriptorTable*;
    };

    class DescriptorTablePool final : public ObjectPool<DescriptorTablePoolTraits>,
                                      public DescriptorPoolBase
    {
        using BasePool = ObjectPool<DescriptorTablePoolTraits>;
    public:
        DescriptorHandle AllocateHandle() override
        {
            ASSERT(false, "DescriptorHandle can not create by DescriptorTablePool");
            return BasePool::Allocate(1)->GetOffset();
        }

        void ReleaseHandle(DescriptorHandle& handle) override
        {
            ASSERT(false, "DescriptorHandle can not release by DescriptorTablePool");
            DescriptorTable table(handle, 1);
            BasePool::DeAllocate(&table);
        }

        DescriptorTable AllocateTable(uint32_t count = 1) override
        {
            return *BasePool::Allocate(count);
        }

        void ReleaseTable(DescriptorTable& table) override
        {
            BasePool::DeAllocate(&table);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandleForTable(DescriptorTable table) const override
        {
            return BasePool::GetFactory().GetD3D12CPUDescriptorTable(table);
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuNativeHandleForTable(DescriptorTable table) const override
        {
            return BasePool::GetFactory().GetD3D12GPUDescriptorTable(table);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandle(DescriptorHandle handle) const override
        {
            return BasePool::GetFactory().GetD3D12CPUDescriptorTable(DescriptorTable(handle, 1));
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuNativeHandle(DescriptorHandle handle) const override
        {
            return BasePool::GetFactory().GetD3D12GPUDescriptorTable(DescriptorTable(handle, 1));
        }

        void Collect() override
        {
            BasePool::Collect();
        }
    };

    //! This class defines a Descriptor pool which manages all the descriptors used for binding resources
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
            uint32_t descriptorCountForPool,
            uint32_t collectLatency = RHI::Limits::Device::FrameCountMax);

        //! Initialize a descriptor pool mapping a range of descriptors from a parent heap.
        void InitPooledRange(ID3D12DeviceX* device, ID3D12DescriptorHeap* heap, uint32_t offset, uint32_t count, uint32_t collectLatency = RHI::Limits::Device::FrameCountMax);

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

        //! Release the underlying D3D12 descriptor heap and internal pool immediately,
        //! rather than waiting for the destructor. This allows ReportLiveDeviceObjects
        //! to see zero live objects when called after factory shutdown.
        void Shutdown();

        //! Garbage collection for freed handles or tables
        void Collect();
        //Get native pointers from the heap
        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandleForTable(DescriptorTable table) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuNativeHandleForTable(DescriptorTable table) const;
        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandle(DescriptorHandle handle) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuNativeHandle(DescriptorHandle handle) const;

    private:
        ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;
        eastl::unique_ptr<DescriptorPoolBase> m_internalPool;
    };
    
}
