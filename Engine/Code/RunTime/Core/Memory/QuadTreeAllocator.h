#pragma once

#include <cstdint>

#include <EASTL/array.h>

namespace Spark
{
    //! Buddy allocation over a square power-of-two region. A block splits into four
    //! quadrants, and merges back only once all four are free again — so the region does not
    //! drift into a state where the free area exists but no whole block can be cut from it.
    //!
    //! Levels count DOWN from the root: level 0 is the whole region, level L has 4^L blocks
    //! of side 1/2^L. A LARGER level number means a SMALLER block.
    //!
    //! Speaks in BLOCKS. What a block measures, and whether the caller insets a border
    //! inside one, is the caller's business — an atlas that reserves a gutter and one that
    //! packs edge to edge want the same allocation and different arithmetic.
    //!
    //! MaxLevel is a template parameter so the node array is sized at compile time and the
    //! allocator never touches the heap: MaxLevel 4 is 341 nodes of one byte.
    template<uint32_t MaxLevel>
    class QuadTreeAllocator
    {
    public:
        //! kNodeCount shifts by 2 * kLevelCount, which is undefined past the width of the
        //! type — and silently so, since the shift is the only thing that would object. The
        //! bound is far beyond the absurd anyway: level 14 alone is 268 million blocks.
        static_assert(MaxLevel < 15, "MaxLevel overflows the shift kNodeCount is built on");

        static constexpr uint32_t kLevelCount = MaxLevel + 1;

        //! Nodes are numbered breadth first, and within a level the four children of one
        //! parent are adjacent — which is what lets Free test a sibling set by scanning four
        //! consecutive entries. That adjacency makes the order within a level Morton, not
        //! row major, so Decode has to de-interleave rather than divide.
        static constexpr uint32_t kNodeCount = ((1u << (2 * kLevelCount)) - 1) / 3;

        //! No block free at the requested level. Distinct from node 0, which is the root.
        static constexpr uint32_t kInvalidNode = ~0u;

        struct Block
        {
            uint32_t m_level = 0;
            uint32_t m_x     = 0;   //! In blocks of its own level, not of the finest level.
            uint32_t m_y     = 0;
        };

        //! Cuts one block at the given level, splitting coarser blocks as needed. Returns
        //! kInvalidNode when no block of that level can be cut, which is not an error — the
        //! caller decides whether to retry coarser, retry finer, or go without.
        uint32_t Allocate(uint32_t level)
        {
            if (level > MaxLevel)
            {
                return kInvalidNode;
            }
            return AllocateFrom(0, 0, level);
        }

        //! Hands a block back, merging it into its parent as far up as the siblings allow.
        void Free(uint32_t node)
        {
            if (node >= kNodeCount || m_nodes[node] != NodeState::Used)
            {
                return;
            }

            m_nodes[node] = NodeState::Free;

            // Walk up for as long as this node completes a set of four free siblings. A
            // parent has children by definition, so it is Split and can only become Free.
            while (node != 0)
            {
                const uint32_t parent     = ParentOf(node);
                const uint32_t firstChild = FirstChildOf(parent);
                for (uint32_t i = 0; i < 4; ++i)
                {
                    if (m_nodes[firstChild + i] != NodeState::Free)
                    {
                        return;
                    }
                }
                m_nodes[parent] = NodeState::Free;
                node            = parent;
            }
        }

        void Reset()
        {
            m_nodes.fill(NodeState::Free);
        }

        //! Level and position of a node, both derived from the id — the allocator stores
        //! neither.
        static Block Decode(uint32_t node)
        {
            Block block;
            block.m_level = LevelOf(node);

            // Morton: the index within a level carries one x bit and one y bit per level of
            // depth, x in the even positions and y in the odd ones.
            const uint32_t index = node - LevelBaseOf(block.m_level);
            for (uint32_t bit = 0; bit < block.m_level; ++bit)
            {
                block.m_x |= ((index >> (2 * bit)) & 1u) << bit;
                block.m_y |= ((index >> (2 * bit + 1)) & 1u) << bit;
            }
            return block;
        }

        //! First node id of a level. Level L holds 4^L nodes, so the levels before it sum to
        //! (4^L - 1) / 3.
        static constexpr uint32_t LevelBaseOf(uint32_t level)
        {
            return ((1u << (2 * level)) - 1) / 3;
        }

        static constexpr uint32_t LevelOf(uint32_t node)
        {
            uint32_t level = 0;
            while (level < MaxLevel && node >= LevelBaseOf(level + 1))
            {
                ++level;
            }
            return level;
        }

    private:
        //! Free means the whole subtree is available, Used means this exact node is handed
        //! out, Split means the node is subdivided and only its descendants can be handed
        //! out.
        enum class NodeState : uint8_t
        {
            Free,
            Split,
            Used,
        };

        static constexpr uint32_t ParentOf(uint32_t node)
        {
            const uint32_t level = LevelOf(node);
            return LevelBaseOf(level - 1) + (node - LevelBaseOf(level)) / 4;
        }

        static constexpr uint32_t FirstChildOf(uint32_t node)
        {
            const uint32_t level = LevelOf(node);
            return LevelBaseOf(level + 1) + (node - LevelBaseOf(level)) * 4;
        }

        //! Depth first, first fit. The tree is tiny — 341 nodes at MaxLevel 4 — so a search
        //! costs less than the bookkeeping per-level free lists would need to stay correct
        //! across merges.
        uint32_t AllocateFrom(uint32_t node, uint32_t nodeLevel, uint32_t wantedLevel)
        {
            if (nodeLevel == wantedLevel)
            {
                if (m_nodes[node] != NodeState::Free)
                {
                    return kInvalidNode;
                }
                m_nodes[node] = NodeState::Used;
                return node;
            }

            if (m_nodes[node] == NodeState::Used)
            {
                return kInvalidNode;
            }

            const uint32_t firstChild = FirstChildOf(node);
            for (uint32_t i = 0; i < 4; ++i)
            {
                const uint32_t taken = AllocateFrom(firstChild + i, nodeLevel + 1, wantedLevel);
                if (taken != kInvalidNode)
                {
                    // Only now, once a descendant is actually handed out. Marking on the way
                    // down would leave a failed search behind as a Split node whose children
                    // are all free — a state Free() can never clean up, since nothing was
                    // taken and so nothing will be given back.
                    m_nodes[node] = NodeState::Split;
                    return taken;
                }
            }
            return kInvalidNode;
        }

        //! NodeState::Free is 0, so the default member initializer leaves the whole region
        //! available.
        eastl::array<NodeState, kNodeCount> m_nodes {};
    };
}
