#include <gtest/gtest.h>

#include <random>

#include <EASTL/algorithm.h>
#include <EASTL/map.h>
#include <EASTL/vector.h>

#include <Memory/TLSFAllocator.h>

using namespace Spark;

namespace
{
    using Allocation = TLSFAllocator::Allocation;

    Allocation Expect(TLSFAllocator& allocator, uint64_t size, uint64_t alignment = 1)
    {
        const Allocation allocation = allocator.Allocate(size, alignment);
        EXPECT_TRUE(allocation.IsValid()) << "size " << size << ", alignment " << alignment;
        return allocation;
    }
}

TEST(TLSFAllocatorTests, FreshSpacePacksFromZero)
{
    TLSFAllocator allocator;
    allocator.Init(1000);
    EXPECT_EQ(allocator.GetExtent(), 0u);

    EXPECT_EQ(Expect(allocator, 100).m_offset, 0u);
    EXPECT_EQ(Expect(allocator, 200).m_offset, 100u);
    EXPECT_EQ(Expect(allocator, 300).m_offset, 300u);
    EXPECT_EQ(allocator.GetExtent(), 600u);
}

TEST(TLSFAllocatorTests, HoleABinLargerIsFilledBeforeTheTail)
{
    TLSFAllocator allocator;
    allocator.Init(1000);
    Expect(allocator, 100);
    const Allocation b = Expect(allocator, 120);
    Expect(allocator, 100);

    allocator.Free(b.m_node);
    EXPECT_EQ(Expect(allocator, 100).m_offset, 100u);
    EXPECT_EQ(allocator.GetExtent(), 320u);
}

TEST(TLSFAllocatorTests, SmallSizesFillHolesExactly)
{
    // Below 16 every size has a bin of its own: a slot pool's one-unit allocations always
    // reuse a hole first.
    TLSFAllocator allocator;
    allocator.Init(1000);
    Allocation slots[4];
    for (Allocation& slot : slots)
    {
        slot = Expect(allocator, 1);
    }
    allocator.Free(slots[1].m_node);
    EXPECT_EQ(Expect(allocator, 1).m_offset, 1u);
    EXPECT_EQ(allocator.GetExtent(), 4u);
}

TEST(TLSFAllocatorTests, SameSizeHoleIsReused)
{
    // 100 is rounded up past its own bin; the head of that bin is looked at first.
    TLSFAllocator allocator;
    allocator.Init(1000);
    Expect(allocator, 100);
    const Allocation b = Expect(allocator, 100);
    Expect(allocator, 100);

    allocator.Free(b.m_node);
    EXPECT_EQ(Expect(allocator, 100).m_offset, 100u);
    EXPECT_EQ(allocator.GetExtent(), 300u);
}

TEST(TLSFAllocatorTests, OnlyTheHeadOfTheOwnBinIsLookedAt)
{
    // 103 and 97 share the bin [96, 104). Freed last, 97 heads it and is too small for 100, so
    // the tail is taken although 103 would fit. Smaller requests round up into that bin, where
    // any block fits: they take 97, then 103.
    TLSFAllocator allocator;
    allocator.Init(1000);
    const Allocation a = Expect(allocator, 103);
    Expect(allocator, 1);
    const Allocation b = Expect(allocator, 97);
    Expect(allocator, 1);

    allocator.Free(a.m_node);
    allocator.Free(b.m_node);
    EXPECT_EQ(Expect(allocator, 100).m_offset, 202u);
    EXPECT_EQ(Expect(allocator, 90).m_offset, 104u);
    EXPECT_EQ(Expect(allocator, 95).m_offset, 0u);
}

TEST(TLSFAllocatorTests, ExtentFallsBackAsTheEndIsFreed)
{
    TLSFAllocator allocator;
    allocator.Init(1000);
    const Allocation a = Expect(allocator, 100);
    const Allocation b = Expect(allocator, 100);
    const Allocation c = Expect(allocator, 100);

    allocator.Free(b.m_node);
    EXPECT_EQ(allocator.GetExtent(), 300u);
    allocator.Free(c.m_node);
    EXPECT_EQ(allocator.GetExtent(), 100u);
    allocator.Free(a.m_node);
    EXPECT_EQ(allocator.GetExtent(), 0u);
}

TEST(TLSFAllocatorTests, FullSpaceIsAllocatedAndNothingMore)
{
    TLSFAllocator allocator;
    allocator.Init(1000);
    const Allocation all = Expect(allocator, 1000);
    EXPECT_EQ(all.m_offset, 0u);
    EXPECT_EQ(allocator.GetExtent(), 1000u);
    EXPECT_FALSE(allocator.Allocate(1).IsValid());
}

TEST(TLSFAllocatorTests, FailsWhenNothingFits)
{
    TLSFAllocator allocator;
    allocator.Init(100);
    Expect(allocator, 60);
    EXPECT_FALSE(allocator.Allocate(50).IsValid());
    EXPECT_FALSE(allocator.Allocate(0).IsValid());
    EXPECT_FALSE(allocator.Allocate(101).IsValid());
    EXPECT_FALSE(allocator.Allocate(30, 64).IsValid());
    EXPECT_TRUE(allocator.Allocate(40).IsValid());
}

TEST(TLSFAllocatorTests, AlignmentPaddingStaysFree)
{
    TLSFAllocator allocator;
    allocator.Init(1000);
    Expect(allocator, 3);
    EXPECT_EQ(Expect(allocator, 8, 8).m_offset, 8u);
    // The five bytes skipped to align it.
    EXPECT_EQ(Expect(allocator, 5).m_offset, 3u);
    EXPECT_EQ(allocator.GetExtent(), 16u);
}

TEST(TLSFAllocatorTests, AlignmentNeedNotBeAPowerOfTwo)
{
    TLSFAllocator allocator;
    allocator.Init(1000);
    Expect(allocator, 5);
    const Allocation a = Expect(allocator, 20, 12);
    EXPECT_EQ(a.m_offset, 12u);
    const Allocation b = Expect(allocator, 7, 20);
    EXPECT_EQ(b.m_offset % 20, 0u);
    EXPECT_GE(b.m_offset, 32u);
}

TEST(TLSFAllocatorTests, NoLimitOnLiveAllocations)
{
    constexpr uint64_t kCount = 200000;
    TLSFAllocator allocator;
    allocator.Init(kCount);

    eastl::vector<Allocation> allocations;
    for (uint64_t i = 0; i < kCount; ++i)
    {
        allocations.push_back(allocator.Allocate(1));
        ASSERT_TRUE(allocations.back().IsValid());
        ASSERT_EQ(allocations.back().m_offset, i);
    }
    EXPECT_FALSE(allocator.Allocate(1).IsValid());

    for (uint64_t i = 0; i < kCount; i += 2)
    {
        allocator.Free(allocations[i].m_node);
    }
    EXPECT_EQ(allocator.GetExtent(), kCount);
    for (uint64_t i = 0; i < kCount; i += 2)
    {
        ASSERT_TRUE(allocator.Allocate(1).IsValid());
    }
    EXPECT_FALSE(allocator.Allocate(1).IsValid());
}

TEST(TLSFAllocatorTests, SpaceBeyondFourGigabytes)
{
    constexpr uint64_t kCapacity = uint64_t(1) << 40;
    constexpr uint64_t kSize     = uint64_t(1) << 36;
    TLSFAllocator allocator;
    allocator.Init(kCapacity);

    for (uint64_t i = 0; i < kCapacity / kSize; ++i)
    {
        EXPECT_EQ(Expect(allocator, kSize).m_offset, i * kSize);
    }
    EXPECT_EQ(allocator.GetExtent(), kCapacity);
    EXPECT_FALSE(allocator.Allocate(1).IsValid());
}

TEST(TLSFAllocatorTests, RandomAgainstAReferenceModel)
{
    constexpr uint64_t kCapacity = 1 << 20;
    TLSFAllocator allocator;
    allocator.Init(kCapacity);

    std::mt19937 random(1234);
    const uint64_t kAlignments[] = { 1, 2, 4, 12, 16, 256 };

    struct Live
    {
        uint64_t m_end  = 0;
        uint32_t m_node = 0;
    };
    eastl::map<uint64_t, Live> live;   // By offset.

    for (int step = 0; step < 50000; ++step)
    {
        const bool allocate = live.empty() || random() % 100 < 55;
        if (allocate)
        {
            const uint64_t size      = 1 + random() % 4096;
            const uint64_t alignment = kAlignments[random() % 6];
            const Allocation a = allocator.Allocate(size, alignment);
            if (!a.IsValid())
            {
                continue;
            }

            ASSERT_EQ(a.m_offset % alignment, 0u);
            ASSERT_LE(a.m_offset + size, kCapacity);
            auto next = live.lower_bound(a.m_offset);
            if (next != live.end())
            {
                ASSERT_GE(next->first, a.m_offset + size) << "overlaps the allocation after it";
            }
            if (next != live.begin())
            {
                ASSERT_LE(eastl::prev(next)->second.m_end, a.m_offset) << "overlaps the allocation before it";
            }
            live[a.m_offset] = Live{ a.m_offset + size, a.m_node };
        }
        else
        {
            auto it = live.begin();
            eastl::advance(it, random() % live.size());
            allocator.Free(it->second.m_node);
            live.erase(it);
        }

        const uint64_t expectedExtent = live.empty() ? 0 : live.rbegin()->second.m_end;
        ASSERT_EQ(allocator.GetExtent(), expectedExtent);
    }

    for (const auto& entry : live)
    {
        allocator.Free(entry.second.m_node);
    }
    EXPECT_EQ(allocator.GetExtent(), 0u);
    EXPECT_EQ(Expect(allocator, kCapacity).m_offset, 0u);
}
