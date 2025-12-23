/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- MemoryAllocation use D3D12MA::Allocation
 *  -- Remove MemoryView name
 */

#pragma once

#include <Object/Base.h>
#include <MemoryEnums.h>
#include "DX12.h"
#include "Memory.h"
#include "3rdParty/D3D12MA/D3D12MemAlloc.h"

namespace Spark::RHI::DX12
{
    enum class MemoryViewType 
    {
        Image = 0,
        Buffer
    };

    class MemoryView
    {
    public:
        MemoryView() = default;
        MemoryView(D3D12MA::Allocation* allocation, MemoryViewType viewType, size_t offset, size_t size, size_t alignment);

        /// Supports only move construction / assignment.
        /// Copying is disallowed as it may lead to double frees of device allocations
        MemoryView(const MemoryView& rhs) = delete;
        MemoryView(MemoryView&& rhs) = default;
        MemoryView& operator=(const MemoryView& rhs) = delete;
        MemoryView& operator=(MemoryView&& rhs) = default;

        bool IsValid() const;

        /// Returns the offset relative to the base memory address in bytes.
        size_t GetOffset() const;

        /// Returns the size of the memory view region in bytes.
        size_t GetSize() const;

        /// Returns the alignment of the memory view region in bytes.
        size_t GetAlignment() const;

        /// Returns a pointer to the memory chunk this view is sub-allocated from.
        Memory* GetMemory() const;

        // Returns a pointer to the D3D12MA allocations that contains this view
        D3D12MA::Allocation* GetMemoryAllocation() const;

        /// A convenience method to map the resource region spanned by the view for CPU access.
        CpuVirtualAddress Map(RHI::HostMemoryAccess hostAccess) const;

        /// A convenience method for unmapping the resource region spanned by the view.
        void Unmap(RHI::HostMemoryAccess hostAccess) const;

        /// Returns the GPU address, offset to match the view.
        GpuVirtualAddress GetGpuAddress() const;

        // Heap the Memory was allocated in. Will be nullptr for committed resources
        ID3D12Heap* GetHeap() const;

        // Offset in the heap that the Memory is allocated in. Will be zero for committed resources
        size_t GetHeapOffset();
    
    private:
        void Construct();

        D3D12MA::Allocation* m_memoryAllocation = nullptr;

        /// The GPU address of the memory view, offset from the base memory location.
        GpuVirtualAddress m_gpuAddress = 0;

        /// A byte offset into the memory chunk.
        size_t m_offset = 0;

        /// The size in bytes of the the momery view.
        size_t m_size = 0;

        /// The alignment in bytes of the momery view.
        size_t m_alignment = 0;

        /// momery view type
        MemoryViewType m_viewType;
    };
}