#pragma once

#include <RHI/HardwareQueue.h>
#include <RHI/MemoryEnums.h>
#include <RHI/Resource/ResourcePoolDescriptor.h>

namespace Spark::RHI
{
    struct TransientResourcePoolDescriptor : public ResourcePoolDescriptor
    {
        TransientResourcePoolDescriptor() = default;
        virtual ~TransientResourcePoolDescriptor() = default;

        //! Per-category budget. Zero means "let the backend pick a default based on
        //! device limits". The image and buffer budgets may map to a single shared
        //! heap (DX12 resource heap tier 2) or to separate heaps (tier 1) — that
        //! decision is internal to the backend.
        uint64_t m_imageBudgetInBytes  = 0;
        uint64_t m_bufferBudgetInBytes = 0;

        HeapMemoryLevel m_heapMemoryLevel = HeapMemoryLevel::Device;

        //! Hardware queues allowed to access resources allocated from this pool.
        //! Resources whose Create / Discard fences reference queues outside this
        //! mask are rejected.
        HardwareQueueClassMask m_supportedPipelines = HardwareQueueClassMask::All;

        //! When true, placed resources cached from prior batches are eligible for
        //! reuse on descriptor match, avoiding repeated CreatePlacedResource costs.
        //! When false the cache is cleared at every OnFrameBegin.
        bool m_allowCrossBatchReuse = true;

        //! When a Create*() request cannot be satisfied within the heap budget or
        //! due to fragmentation, fall back to a non-aliased committed resource and
        //! emit a warning instead of returning failure.
        bool m_allowCommittedFallback = true;
    };
}
