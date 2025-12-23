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

#include "MemoryView.h"

#include "Log/SpdLogSystem.h"

namespace Spark::RHI::DX12
{
    MemoryView::MemoryView(D3D12MA::Allocation* allocation, MemoryViewType viewType, size_t offset, size_t size, size_t alignment)
        : m_memoryAllocation(allocation)
        , m_viewType(viewType)
        , m_offset(offset)
        , m_size(size)
        , m_alignment(alignment)
    {
        Construct();
    }

    bool MemoryView::IsValid() const
    {
        return m_memoryAllocation->GetResource() != nullptr;
    }

    Memory* MemoryView::GetMemory() const
    {
        return m_memoryAllocation->GetResource();
    }

    D3D12MA::Allocation* MemoryView::GetMemoryAllocation() const
    {
        return m_memoryAllocation;
    }

    size_t MemoryView::GetOffset() const
    {
        return m_offset;
    }

    size_t MemoryView::GetSize() const
    {
        return m_size;
    }

    size_t MemoryView::GetAlignment() const
    {
        return m_alignment;
    }

    CpuVirtualAddress MemoryView::Map(RHI::HostMemoryAccess hostAccess) const
    {
        CpuVirtualAddress cpuAddress = nullptr;

        D3D12_RANGE readRange = {};
        if (hostAccess == RHI::HostMemoryAccess::Read)
        {
            readRange.Begin = m_offset;
            readRange.End = m_offset + m_size;
        }
        m_memoryAllocation->GetResource()->Map(0, &readRange, reinterpret_cast<void**>(&cpuAddress));

        // Make sure we return null if the map operation failed.
        if (cpuAddress)
        {
            cpuAddress += m_offset;
        }
        
        return cpuAddress;
    }

    void MemoryView::Unmap(RHI::HostMemoryAccess hostAccess) const
    {

        D3D12_RANGE writeRange = {};
        if (hostAccess == RHI::HostMemoryAccess::Write)
        {
            writeRange.Begin = m_offset;
            writeRange.End = m_offset + m_size;
        }
        m_memoryAllocation->GetResource()->Unmap(0, &writeRange);
    }

    GpuVirtualAddress MemoryView::GetGpuAddress() const
    {
        return m_gpuAddress;
    }

    ID3D12Heap* MemoryView::GetHeap() const
    {
        return m_memoryAllocation->GetHeap();
    }

    size_t MemoryView::GetHeapOffset()
    {
        return m_memoryAllocation->GetOffset();
    }

    void MemoryView::Construct()
    {
        if (m_memoryAllocation->GetResource())
        {
            if (m_viewType == MemoryViewType::Image)
            {
                // Image resources will always be zero. The address will only be valid for buffer 
                m_gpuAddress = 0;

                ASSERT(m_offset == 0, "Image memory does not support local offsets.");
            }
            else
            {
                m_gpuAddress = m_memoryAllocation->GetResource()->GetGPUVirtualAddress();
            }

            m_gpuAddress += m_offset;
        }
    }
}