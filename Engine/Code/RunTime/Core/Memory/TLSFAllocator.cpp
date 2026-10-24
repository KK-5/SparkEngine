#include "TLSFAllocator.h"

#include <EASTL/bit.h>

#include <Log/ILogSystem.h>

namespace Spark
{
    namespace
    {
        //! value must not be 0.
        uint32_t HighestBit(uint64_t value)
        {
            return 63u - static_cast<uint32_t>(eastl::countl_zero(value));
        }

        //! value must not be 0. value & (~value + 1) keeps only the lowest set bit.
        uint32_t LowestBit(uint64_t value)
        {
            return HighestBit(value & (~value + 1));
        }

        //! The count lowest bits of value.
        uint64_t LowBits(uint64_t value, uint32_t count)
        {
            return count >= 64 ? value : value & ((uint64_t(1) << count) - 1);
        }

        //! Every bit from index up: masks out the bins below one.
        uint64_t BitsFrom(uint32_t index)
        {
            return index >= 64 ? 0 : ~uint64_t(0) << index;
        }
    }

    // Like a float of kSubBinBits mantissa bits: the highest set bit is the exponent and picks
    // the top bin, the bits right below it the sub bin. The top bin 0 holds the sizes below
    // kSubBinCount, one bin each, which is what the + 1 skips.
    uint32_t TLSFAllocator::BinRoundDown(uint64_t size)
    {
        if (size < kSubBinCount)
        {
            return static_cast<uint32_t>(size);
        }
        const uint32_t mantissaShift = HighestBit(size) - kSubBinBits;
        // Drops the highest bit, which every size of the top bin shares.
        const uint32_t mantissa = static_cast<uint32_t>(LowBits(size >> mantissaShift, kSubBinBits));
        return BinOf(mantissaShift + 1, mantissa);
    }

    uint32_t TLSFAllocator::BinRoundUp(uint64_t size)
    {
        const uint32_t bin = BinRoundDown(size);
        if (size < kSubBinCount)
        {
            return bin;
        }
        // Bits below the mantissa put the size past its bin's lower edge. A carry out of the
        // mantissa is the next top bin, as in a float.
        const uint32_t mantissaShift = HighestBit(size) - kSubBinBits;
        return LowBits(size, mantissaShift) != 0 ? bin + 1 : bin;
    }

    void TLSFAllocator::Init(uint64_t capacity)
    {
        m_nodes.clear();
        m_unusedNodes.clear();
        m_binHeads.fill(kInvalidNode);
        m_usedTopBins = 0;
        m_usedSubBins.fill(0);
        m_capacity = capacity;
        m_lastNode = kInvalidNode;

        if (capacity != 0)
        {
            m_lastNode = NewNode(0, capacity);
            InsertIntoBin(m_lastNode);
        }
    }

    TLSFAllocator::Allocation TLSFAllocator::Allocate(uint64_t size, uint64_t alignment)
    {
        alignment = alignment == 0 ? 1 : alignment;
        if (size == 0 || size > m_capacity || alignment - 1 > m_capacity - size)
        {
            return {};
        }

        // Large enough wherever the block starts.
        const uint64_t request = size + alignment - 1;
        const uint32_t ownBin  = BinRoundDown(request);

        // Rounding up skips the request's own bin. Its head, the block freed last, is worth one
        // look: a block of the same size freed and asked for again is the common reuse.
        uint32_t node = m_binHeads[ownBin];
        if (node == kInvalidNode || m_nodes[node].m_size < request)
        {
            const uint32_t bin = FindFreeBin(BinRoundUp(request));
            node = bin != kInvalidNode ? m_binHeads[bin] : FindInBin(ownBin, request);
            if (node == kInvalidNode)
            {
                return {};
            }
        }
        RemoveFromBin(node);

        const uint64_t blockStart = m_nodes[node].m_offset;
        const uint64_t blockEnd   = blockStart + m_nodes[node].m_size;
        const uint64_t start      = (blockStart + alignment - 1) / alignment * alignment;
        const uint64_t end        = start + size;

        // The found block becomes the allocation; what is left on either side stays free. Its
        // neighbours are allocated, free ones having merged with it, so neither part merges.
        if (start != blockStart)
        {
            const uint32_t front = NewNode(blockStart, start - blockStart);
            m_nodes[front].m_neighborPrev = m_nodes[node].m_neighborPrev;
            m_nodes[front].m_neighborNext = node;
            if (m_nodes[node].m_neighborPrev != kInvalidNode)
            {
                m_nodes[m_nodes[node].m_neighborPrev].m_neighborNext = front;
            }
            m_nodes[node].m_neighborPrev = front;
            InsertIntoBin(front);
        }

        if (end != blockEnd)
        {
            const uint32_t back = NewNode(end, blockEnd - end);
            m_nodes[back].m_neighborPrev = node;
            m_nodes[back].m_neighborNext = m_nodes[node].m_neighborNext;
            if (m_nodes[node].m_neighborNext != kInvalidNode)
            {
                m_nodes[m_nodes[node].m_neighborNext].m_neighborPrev = back;
            }
            m_nodes[node].m_neighborNext = back;
            InsertIntoBin(back);
            if (m_lastNode == node)
            {
                m_lastNode = back;
            }
        }

        m_nodes[node].m_offset = start;
        m_nodes[node].m_size   = size;
        m_nodes[node].m_used   = true;
        return Allocation{ start, node };
    }

    void TLSFAllocator::Free(uint32_t node)
    {
        if (node >= m_nodes.size() || !m_nodes[node].m_used)
        {
            ASSERT(false, "[TLSFAllocator] Freed node {}, which is not allocated.", node);
            return;
        }
        m_nodes[node].m_used = false;

        const uint32_t prev = m_nodes[node].m_neighborPrev;
        if (prev != kInvalidNode && !m_nodes[prev].m_used)
        {
            RemoveFromBin(prev);
            m_nodes[node].m_offset = m_nodes[prev].m_offset;
            m_nodes[node].m_size  += m_nodes[prev].m_size;
            m_nodes[node].m_neighborPrev = m_nodes[prev].m_neighborPrev;
            if (m_nodes[node].m_neighborPrev != kInvalidNode)
            {
                m_nodes[m_nodes[node].m_neighborPrev].m_neighborNext = node;
            }
            DeleteNode(prev);
        }

        const uint32_t next = m_nodes[node].m_neighborNext;
        if (next != kInvalidNode && !m_nodes[next].m_used)
        {
            RemoveFromBin(next);
            m_nodes[node].m_size += m_nodes[next].m_size;
            m_nodes[node].m_neighborNext = m_nodes[next].m_neighborNext;
            if (m_nodes[node].m_neighborNext != kInvalidNode)
            {
                m_nodes[m_nodes[node].m_neighborNext].m_neighborPrev = node;
            }
            if (m_lastNode == next)
            {
                m_lastNode = node;
            }
            DeleteNode(next);
        }

        InsertIntoBin(node);
    }

    uint64_t TLSFAllocator::GetExtent() const
    {
        if (m_lastNode == kInvalidNode)
        {
            return 0;
        }
        const Node& last = m_nodes[m_lastNode];
        return last.m_used ? m_capacity : last.m_offset;
    }

    uint32_t TLSFAllocator::NewNode(uint64_t offset, uint64_t size)
    {
        uint32_t node;
        if (!m_unusedNodes.empty())
        {
            node = m_unusedNodes.back();
            m_unusedNodes.pop_back();
            m_nodes[node] = Node{};
        }
        else
        {
            node = static_cast<uint32_t>(m_nodes.size());
            m_nodes.push_back();
        }
        m_nodes[node].m_offset = offset;
        m_nodes[node].m_size   = size;
        return node;
    }

    void TLSFAllocator::DeleteNode(uint32_t node)
    {
        m_unusedNodes.push_back(node);
    }

    void TLSFAllocator::InsertIntoBin(uint32_t node)
    {
        const uint32_t bin  = BinRoundDown(m_nodes[node].m_size);
        const uint32_t head = m_binHeads[bin];
        m_nodes[node].m_binPrev = kInvalidNode;
        m_nodes[node].m_binNext = head;
        if (head != kInvalidNode)
        {
            m_nodes[head].m_binPrev = node;
        }
        m_binHeads[bin] = node;
        MarkBinNonEmpty(bin);
    }

    void TLSFAllocator::RemoveFromBin(uint32_t node)
    {
        const Node& n = m_nodes[node];
        if (n.m_binPrev != kInvalidNode)
        {
            m_nodes[n.m_binPrev].m_binNext = n.m_binNext;
        }
        if (n.m_binNext != kInvalidNode)
        {
            m_nodes[n.m_binNext].m_binPrev = n.m_binPrev;
        }

        const uint32_t bin = BinRoundDown(n.m_size);
        if (m_binHeads[bin] == node)
        {
            m_binHeads[bin] = n.m_binNext;
            if (n.m_binNext == kInvalidNode)
            {
                MarkBinEmpty(bin);
            }
        }
    }

    void TLSFAllocator::MarkBinNonEmpty(uint32_t bin)
    {
        const uint32_t top = TopBinOf(bin);
        m_usedSubBins[top] |= static_cast<uint8_t>(1u << SubBinOf(bin));
        m_usedTopBins      |= uint64_t(1) << top;
    }

    void TLSFAllocator::MarkBinEmpty(uint32_t bin)
    {
        const uint32_t top = TopBinOf(bin);
        m_usedSubBins[top] &= static_cast<uint8_t>(~(1u << SubBinOf(bin)));
        if (m_usedSubBins[top] == 0)
        {
            m_usedTopBins &= ~(uint64_t(1) << top);
        }
    }

    uint32_t TLSFAllocator::FindInBin(uint32_t bin, uint64_t size) const
    {
        // Linear, but only reached when nothing larger is free: the space fails only when full.
        for (uint32_t node = m_binHeads[bin]; node != kInvalidNode; node = m_nodes[node].m_binNext)
        {
            if (m_nodes[node].m_size >= size)
            {
                return node;
            }
        }
        return kInvalidNode;
    }

    uint32_t TLSFAllocator::FindFreeBin(uint32_t minBin) const
    {
        // A lower bit is a smaller bin, so the lowest set bit of a mask is the smallest non-empty
        // bin it allows.
        const uint32_t top = TopBinOf(minBin);
        if (top >= kTopBinCount)
        {
            return kInvalidNode;
        }

        const uint64_t subBins = m_usedSubBins[top] & BitsFrom(SubBinOf(minBin));
        if (subBins != 0)
        {
            return BinOf(top, LowestBit(subBins));
        }

        // No bin of this top bin is large enough: the smallest of the next non-empty top bin.
        const uint64_t topBins = m_usedTopBins & BitsFrom(top + 1);
        if (topBins == 0)
        {
            return kInvalidNode;
        }
        const uint32_t nextTop = LowestBit(topBins);
        return BinOf(nextTop, LowestBit(m_usedSubBins[nextTop]));
    }
}
