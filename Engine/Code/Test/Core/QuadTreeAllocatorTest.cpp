#include <gtest/gtest.h>

#include <EASTL/vector.h>

#include <Memory/QuadTreeAllocator.h>

using namespace Spark;

namespace
{
    //! Levels 0..2 — one 1x1 root, four halves, sixteen quarters. Small enough that every
    //! expectation below can be counted by hand, and the structure is identical at any depth.
    constexpr uint32_t kMaxLevel = 2;
    using Allocator = QuadTreeAllocator<kMaxLevel>;

    //! A block's extent in units of the FINEST level, which is the common denominator any
    //! two blocks can be compared in regardless of the levels they were cut at.
    struct Extent
    {
        uint32_t m_x = 0, m_y = 0, m_size = 0;
    };

    Extent ExtentOf(uint32_t node)
    {
        const Allocator::Block block = Allocator::Decode(node);
        const uint32_t         size  = 1u << (kMaxLevel - block.m_level);
        return Extent{ block.m_x * size, block.m_y * size, size };
    }

    bool Overlaps(const Extent& a, const Extent& b)
    {
        return a.m_x < b.m_x + b.m_size && b.m_x < a.m_x + a.m_size
            && a.m_y < b.m_y + b.m_size && b.m_y < a.m_y + a.m_size;
    }

    void ExpectAllDisjoint(const eastl::vector<uint32_t>& nodes)
    {
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            for (size_t j = i + 1; j < nodes.size(); ++j)
            {
                const Extent a = ExtentOf(nodes[i]);
                const Extent b = ExtentOf(nodes[j]);
                EXPECT_FALSE(Overlaps(a, b))
                    << "nodes " << nodes[i] << " and " << nodes[j] << " overlap";
            }
        }
    }
}

TEST(QuadTreeAllocatorTest, NodeCountCoversEveryLevel)
{
    // 1 + 4 + 16.
    EXPECT_EQ(Allocator::kNodeCount, 21u);
    EXPECT_EQ(Allocator::LevelBaseOf(0), 0u);
    EXPECT_EQ(Allocator::LevelBaseOf(1), 1u);
    EXPECT_EQ(Allocator::LevelBaseOf(2), 5u);
}

TEST(QuadTreeAllocatorTest, RootIsOneBlockAndOnlyOne)
{
    Allocator alloc;
    EXPECT_EQ(alloc.Allocate(0), 0u);
    EXPECT_EQ(alloc.Allocate(0), Allocator::kInvalidNode);
    EXPECT_EQ(alloc.Allocate(2), Allocator::kInvalidNode);
}

TEST(QuadTreeAllocatorTest, LevelYieldsExactlyFourToThePower)
{
    Allocator                alloc;
    eastl::vector<uint32_t>  taken;
    for (uint32_t i = 0; i < 16; ++i)
    {
        const uint32_t node = alloc.Allocate(2);
        ASSERT_NE(node, Allocator::kInvalidNode) << "at block " << i;
        taken.push_back(node);
    }
    EXPECT_EQ(alloc.Allocate(2), Allocator::kInvalidNode);
    ExpectAllDisjoint(taken);
}

//! Children are numbered Morton, so decoding a level's index by division would put two
//! blocks on top of each other. Pins the de-interleave.
TEST(QuadTreeAllocatorTest, DecodeUsesMortonOrderNotRowMajor)
{
    // Level 1 occupies nodes 1..4, one per quadrant.
    EXPECT_EQ(Allocator::Decode(1).m_x, 0u);
    EXPECT_EQ(Allocator::Decode(1).m_y, 0u);
    EXPECT_EQ(Allocator::Decode(2).m_x, 1u);
    EXPECT_EQ(Allocator::Decode(2).m_y, 0u);
    EXPECT_EQ(Allocator::Decode(3).m_x, 0u);
    EXPECT_EQ(Allocator::Decode(3).m_y, 1u);
    EXPECT_EQ(Allocator::Decode(4).m_x, 1u);
    EXPECT_EQ(Allocator::Decode(4).m_y, 1u);

    // Node 9 is the first child of node 2, the top-right half, so it sits in that half.
    EXPECT_EQ(Allocator::Decode(9).m_level, 2u);
    EXPECT_EQ(Allocator::Decode(9).m_x, 2u);
    EXPECT_EQ(Allocator::Decode(9).m_y, 0u);
}

TEST(QuadTreeAllocatorTest, MixedLevelsNeverOverlap)
{
    Allocator               alloc;
    eastl::vector<uint32_t> taken;

    // One quarter, then two halves, then quarters until the region is full.
    taken.push_back(alloc.Allocate(2));
    taken.push_back(alloc.Allocate(1));
    taken.push_back(alloc.Allocate(1));
    for (uint32_t i = 0; i < 3; ++i)
    {
        taken.push_back(alloc.Allocate(2));
    }
    for (uint32_t node : taken)
    {
        ASSERT_NE(node, Allocator::kInvalidNode);
    }

    ExpectAllDisjoint(taken);

    // 1 + 4 + 4 + 3 = 12 of the 16 finest blocks are spoken for, and the four left are the
    // one quadrant nothing has touched — so a half still comes out whole.
    EXPECT_EQ(alloc.Allocate(1), 4u);
    EXPECT_EQ(alloc.Allocate(2), Allocator::kInvalidNode);
}

//! Free area is not the same as a free block. One quarter held in each quadrant leaves a
//! region a quarter empty that cannot yield a single half.
TEST(QuadTreeAllocatorTest, ScatteredQuartersBlockEveryHalf)
{
    Allocator               alloc;
    eastl::vector<uint32_t> taken;
    for (uint32_t i = 0; i < 16; ++i)
    {
        taken.push_back(alloc.Allocate(2));
    }

    // Level 2 runs 5..20, four consecutive nodes per quadrant, so this frees one from each.
    for (uint32_t quadrant = 0; quadrant < 4; ++quadrant)
    {
        alloc.Free(5 + quadrant * 4);
    }

    EXPECT_EQ(alloc.Allocate(1), Allocator::kInvalidNode);
    for (uint32_t i = 0; i < 4; ++i)
    {
        EXPECT_NE(alloc.Allocate(2), Allocator::kInvalidNode);
    }
    EXPECT_EQ(alloc.Allocate(2), Allocator::kInvalidNode);
}

TEST(QuadTreeAllocatorTest, SiblingsMergeOnlyWhenAllFourAreBack)
{
    Allocator alloc;

    uint32_t quarters[4];
    for (uint32_t i = 0; i < 4; ++i)
    {
        quarters[i] = alloc.Allocate(2);
        ASSERT_NE(quarters[i], Allocator::kInvalidNode);
    }
    // Fill the rest so only this quadrant can ever satisfy a half-sized request.
    for (uint32_t i = 0; i < 12; ++i)
    {
        ASSERT_NE(alloc.Allocate(2), Allocator::kInvalidNode);
    }

    for (uint32_t i = 0; i < 3; ++i)
    {
        alloc.Free(quarters[i]);
        EXPECT_EQ(alloc.Allocate(1), Allocator::kInvalidNode)
            << "merged after only " << (i + 1) << " sibling(s) came back";
    }

    alloc.Free(quarters[3]);
    EXPECT_NE(alloc.Allocate(1), Allocator::kInvalidNode);
}

TEST(QuadTreeAllocatorTest, MergeCascadesToTheRoot)
{
    Allocator               alloc;
    eastl::vector<uint32_t> taken;
    for (uint32_t i = 0; i < 16; ++i)
    {
        taken.push_back(alloc.Allocate(2));
    }
    for (uint32_t node : taken)
    {
        alloc.Free(node);
    }

    // Every level of splits has to have unwound, not just the bottom one.
    EXPECT_EQ(alloc.Allocate(0), 0u);
}

//! A search that finds nothing must not leave the nodes it walked marked as split — the
//! region would then be permanently unable to hand out a coarse block even once empty.
TEST(QuadTreeAllocatorTest, FailedAllocationLeavesNoResidue)
{
    Allocator alloc;

    const uint32_t root = alloc.Allocate(0);
    ASSERT_EQ(root, 0u);
    EXPECT_EQ(alloc.Allocate(2), Allocator::kInvalidNode);
    EXPECT_EQ(alloc.Allocate(1), Allocator::kInvalidNode);

    alloc.Free(root);
    EXPECT_EQ(alloc.Allocate(0), 0u);
}

TEST(QuadTreeAllocatorTest, FreeIgnoresWhatItDidNotHandOut)
{
    Allocator alloc;

    const uint32_t half = alloc.Allocate(1);
    ASSERT_NE(half, Allocator::kInvalidNode);

    alloc.Free(Allocator::kNodeCount);   // out of range
    alloc.Free(0);                       // an ancestor, split rather than used
    alloc.Free(half);
    alloc.Free(half);                    // already back

    EXPECT_EQ(alloc.Allocate(0), 0u);
}

TEST(QuadTreeAllocatorTest, ResetReclaimsEverything)
{
    Allocator alloc;
    for (uint32_t i = 0; i < 16; ++i)
    {
        ASSERT_NE(alloc.Allocate(2), Allocator::kInvalidNode);
    }
    EXPECT_EQ(alloc.Allocate(2), Allocator::kInvalidNode);

    alloc.Reset();
    EXPECT_EQ(alloc.Allocate(0), 0u);
}
