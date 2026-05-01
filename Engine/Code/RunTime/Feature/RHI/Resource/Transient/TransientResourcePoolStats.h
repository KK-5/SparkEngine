#pragma once

#include <cstdint>

namespace Spark::RHI
{
    //! Live snapshot of a TransientResourcePool's footprint and aliasing efficiency.
    //! Cheap to query; intended for HUD overlays and frame profilers.
    struct TransientResourcePoolStats
    {
        //! Bytes currently committed to backing heaps.
        uint64_t m_committedHeapBytes = 0;

        //! Sum of unique resource footprints requested in the current batch.
        //! m_committedHeapBytes < m_requestedBytes implies aliasing has saved memory.
        uint64_t m_requestedBytes = 0;

        //! Number of distinct allocations recorded in the current batch.
        uint32_t m_allocationCount = 0;

        //! Allocations that fell back to committed resources because the aliased
        //! sub-allocator could not place them.
        uint32_t m_committedFallbackCount = 0;

        //! Aliasing barriers the backend determined are required for the batch.
        uint32_t m_aliasingBarrierCount = 0;

        //! Cross-batch placed resource cache statistics.
        uint32_t m_placedResourceCacheHits   = 0;
        uint32_t m_placedResourceCacheMisses = 0;
    };
}
