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
#pragma once

#include <RHI/Resource/Buffer/Buffer.h>

#include "BufferMemoryView.h"

namespace Spark::RHI::DX12
{
    class Buffer final : public RHI::Buffer
    {
        using Base = RHI::Buffer;
    public:
        // Returns the memory view allocated to this buffer.
        const BufferMemoryView& GetMemoryView() const;
        BufferMemoryView& GetMemoryView();

        // The initial state for the graph compiler to use when compiling the resource transition chain.
        D3D12_RESOURCE_STATES m_initialAttachmentState = D3D12_RESOURCE_STATE_COMMON;

        uint64_t GetDeviceAddress() const override;

    private:
        Buffer() = default;

        friend class BufferPool;
        friend class AliasedHeap;
        friend class BufferPoolResolver;

        //////////////////////////////////////////////////////////////////////////
        // RHI::DeviceBuffer
        using RHI::Buffer::SetDescriptor;
        //////////////////////////////////////////////////////////////////////////

        // The buffer memory allocation on the primary heap.
        BufferMemoryView m_memoryView;

        // The number of resolve operations pending for this buffer.
        eastl::atomic<uint32_t> m_pendingResolves = 0;

    };
}