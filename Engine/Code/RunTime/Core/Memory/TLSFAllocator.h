#pragma once

#include <cstdint>

#include <EASTL/array.h>
#include <EASTL/vector.h>

namespace Spark
{
    //! Two-Level Segregated Fit over an address space of [0, capacity). Hands out OFFSETS, not
    //! memory: what the space is, and in what unit it counts, is the caller's.
    //!
    //! Free blocks sit in bins by size: the top level by the highest set bit, each split into
    //! eight by the next three bits. A request takes the head of its own bin if that fits,
    //! else is rounded up to a bin edge, where any block of the first non-empty bin fits, found
    //! with two bit scans: O(1), a good fit rather than the best. Freed blocks merge with free
    //! neighbours.
    //!
    //! The node table grows on demand: no limit on how many allocations are live.
    class TLSFAllocator
    {
    public:
        static constexpr uint32_t kInvalidNode = ~0u;

        struct Allocation
        {
            uint64_t m_offset = 0;
            //! What Free takes. kInvalidNode when the allocation failed.
            uint32_t m_node = kInvalidNode;

            bool IsValid() const { return m_node != kInvalidNode; }
        };

        //! Forgets every allocation.
        void Init(uint64_t capacity);

        //! Any alignment, a power of two or not. The padding in front of the aligned start goes
        //! back as a free block, so alignment wastes nothing. An invalid allocation when no free
        //! block fits, or for a size of 0.
        Allocation Allocate(uint64_t size, uint64_t alignment = 1);

        void Free(uint32_t node);

        //! Every live allocation lies in [0, extent). Falls back once the allocations at the end
        //! are freed, unlike a high-water mark.
        uint64_t GetExtent() const;

        uint64_t GetCapacity() const { return m_capacity; }

    private:
        static constexpr uint32_t kSubBinBits  = 3;
        static constexpr uint32_t kSubBinCount = 1u << kSubBinBits;
        static constexpr uint32_t kTopBinCount = 64;
        static constexpr uint32_t kBinCount    = kTopBinCount * kSubBinCount;

        //! A block of the space, free or allocated. Neighbours are by address; the bin list
        //! links the free blocks of one bin.
        struct Node
        {
            uint64_t m_offset       = 0;
            uint64_t m_size         = 0;
            uint32_t m_neighborPrev = kInvalidNode;
            uint32_t m_neighborNext = kInvalidNode;
            uint32_t m_binPrev      = kInvalidNode;
            uint32_t m_binNext      = kInvalidNode;
            bool     m_used         = false;
        };

        //! A bin index is its top bin times kSubBinCount plus its sub bin.
        static constexpr uint32_t BinOf(uint32_t top, uint32_t sub) { return (top << kSubBinBits) | sub; }
        static constexpr uint32_t TopBinOf(uint32_t bin) { return bin >> kSubBinBits; }
        static constexpr uint32_t SubBinOf(uint32_t bin) { return bin & (kSubBinCount - 1); }

        //! Bin of a free block: every block in bin i is at least BinRoundUp's request for i.
        static uint32_t BinRoundDown(uint64_t size);
        //! First bin all of whose blocks are at least size.
        static uint32_t BinRoundUp(uint64_t size);

        //! The occupancy masks: bit sub of m_usedSubBins[top] for each non-empty bin, and bit
        //! top of m_usedTopBins for each top bin with any.
        void MarkBinNonEmpty(uint32_t bin);
        void MarkBinEmpty(uint32_t bin);

        uint32_t NewNode(uint64_t offset, uint64_t size);
        void     DeleteNode(uint32_t node);

        void InsertIntoBin(uint32_t node);
        void RemoveFromBin(uint32_t node);
        uint32_t FindFreeBin(uint32_t minBin) const;
        //! First block of the bin at least size.
        uint32_t FindInBin(uint32_t bin, uint64_t size) const;

        eastl::vector<Node>     m_nodes;
        eastl::vector<uint32_t> m_unusedNodes;

        eastl::array<uint32_t, kBinCount>   m_binHeads{};
        uint64_t                            m_usedTopBins = 0;
        eastl::array<uint8_t, kTopBinCount> m_usedSubBins{};

        uint64_t m_capacity = 0;
        //! The block ending at m_capacity. Free, its offset is the extent.
        uint32_t m_lastNode = kInvalidNode;
    };
}
