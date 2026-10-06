/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Rename BufferMemoryType::SubAllocated to BufferMemoryType::Shared
 */

#include "BufferMemoryView.h"

namespace Spark::RHI::DX12
{        
    BufferMemoryView::BufferMemoryView(
        MemoryView&& memoryView,
        BufferMemoryType memoryType)
        : MemoryView(eastl::move(memoryView))
        , m_type{memoryType}
    {}

    BufferMemoryView::BufferMemoryView(
        MemoryView&& memoryView,
        Ptr<VirtualBlockAllocation> allocation)
        : MemoryView(eastl::move(memoryView))
        , m_type{BufferMemoryType::Shared}
        , m_virtualBlockAllocation{eastl::move(allocation)}
    {}

    BufferMemoryType BufferMemoryView::GetType() const
    {
        return m_type;
    }

    VirtualBlockAllocation* BufferMemoryView::GetVirtualBlockAllocation() const
    {
        return m_virtualBlockAllocation.get();
    }
}
