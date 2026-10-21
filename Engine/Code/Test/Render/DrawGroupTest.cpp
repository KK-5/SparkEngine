#include <gtest/gtest.h>

#include <EASTL/algorithm.h>
#include <EASTL/vector.h>

#include <DrawList/DrawGroup.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    PipelineVariant Variant(uint32_t priority, RHI::CullMode cullMode = RHI::CullMode::Invalid)
    {
        PipelineVariant variant;
        variant.m_priority = priority;
        variant.m_cullMode = cullMode;
        return variant;
    }

    const PipelineVariant Default    = Variant(0);
    const PipelineVariant TwoSided   = Variant(0, RHI::CullMode::None);
    const PipelineVariant Later      = Variant(1);

    DrawEntry Entry(const PipelineVariant& variant, uint32_t count, uint32_t keyIndex = 0)
    {
        DrawEntry entry;
        entry.m_variant  = variant;
        entry.m_count    = count;
        entry.m_keyIndex = keyIndex;
        return entry;
    }
}

TEST(DrawGroupTest, NoEntriesMakeNoGroup)
{
    eastl::vector<DrawEntry> entries;
    eastl::vector<DrawGroup> groups{ DrawGroup{} };

    GroupDrawEntries(entries, DrawOrder::Any, groups);

    EXPECT_TRUE(groups.empty());
}

TEST(DrawGroupTest, AnyDrawsTheLowerPriorityFirst)
{
    eastl::vector<DrawEntry> entries{ Entry(Later, 1), Entry(Default, 1) };
    eastl::vector<DrawGroup> groups;

    GroupDrawEntries(entries, DrawOrder::Any, groups);

    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].m_variant, Default);
    EXPECT_EQ(groups[1].m_variant, Later);
}

TEST(DrawGroupTest, AnyMakesOneGroupOfKeysOfOneVariant)
{
    // Two keys apart in the input, with one variant in this pass, as GBuffer sees opaque and masked.
    eastl::vector<DrawEntry> entries{ Entry(Default, 3, 7), Entry(TwoSided, 2, 4), Entry(Default, 5, 9) };
    eastl::vector<DrawGroup> groups;

    GroupDrawEntries(entries, DrawOrder::Any, groups);

    ASSERT_EQ(groups.size(), 2u);
    const DrawGroup& merged = groups[0].m_variant == Default ? groups[0] : groups[1];
    EXPECT_EQ(merged.m_maxDraws, 8u);

    // Both keys fall in it.
    auto groupOfKey = [&](uint32_t keyIndex)
    {
        const auto entry = eastl::find_if(entries.begin(), entries.end(),
            [&](const DrawEntry& e) { return e.m_keyIndex == keyIndex; });
        return entry->m_groupIndex;
    };
    EXPECT_EQ(groupOfKey(7), merged.m_index);
    EXPECT_EQ(groupOfKey(9), merged.m_index);
    EXPECT_NE(groupOfKey(4), merged.m_index);
}

TEST(DrawGroupTest, AGroupStartsAfterWhatTheGroupsBeforeItMakeRoomFor)
{
    eastl::vector<DrawEntry> entries{ Entry(Default, 3), Entry(TwoSided, 2), Entry(Later, 4) };
    eastl::vector<DrawGroup> groups;

    GroupDrawEntries(entries, DrawOrder::Any, groups);

    ASSERT_EQ(groups.size(), 3u);
    uint32_t firstDraw = 0;
    for (const DrawGroup& group : groups)
    {
        EXPECT_EQ(group.m_firstDraw, firstDraw);
        firstDraw += group.m_maxDraws;
    }
    EXPECT_EQ(firstDraw, 9u);
}

TEST(DrawGroupTest, BackToFrontKeepsTheOrderAndSplitsAVariantThatComesBack)
{
    eastl::vector<DrawEntry> entries{ Entry(Later, 1), Entry(Default, 1), Entry(Default, 1), Entry(Later, 1) };
    eastl::vector<DrawGroup> groups;

    GroupDrawEntries(entries, DrawOrder::BackToFront, groups);

    ASSERT_EQ(groups.size(), 3u);
    EXPECT_EQ(groups[0].m_variant, Later);
    EXPECT_EQ(groups[1].m_variant, Default);
    EXPECT_EQ(groups[1].m_maxDraws, 2u);
    EXPECT_EQ(groups[2].m_variant, Later);
    EXPECT_EQ(entries[0].m_variant, Later);
    EXPECT_EQ(entries[3].m_variant, Later);
}

//! So the entries, in the order left, are the records of the groups one after another.
TEST(DrawGroupTest, EachEntryFallsInAGroupInOrder)
{
    eastl::vector<DrawEntry> entries{
        Entry(Later, 1), Entry(TwoSided, 2), Entry(Default, 3), Entry(TwoSided, 4), Entry(Default, 5) };

    for (DrawOrder order : { DrawOrder::Any, DrawOrder::BackToFront })
    {
        eastl::vector<DrawEntry> ordered = entries;
        eastl::vector<DrawGroup> groups;

        GroupDrawEntries(ordered, order, groups);

        ASSERT_FALSE(groups.empty());
        eastl::vector<uint32_t> draws(groups.size(), 0);
        uint32_t previous = 0;
        for (const DrawEntry& entry : ordered)
        {
            ASSERT_LT(entry.m_groupIndex, groups.size());
            EXPECT_GE(entry.m_groupIndex, previous);
            EXPECT_LE(entry.m_groupIndex, previous + 1);
            EXPECT_EQ(entry.m_variant, groups[entry.m_groupIndex].m_variant);
            draws[entry.m_groupIndex] += entry.m_count;
            previous = entry.m_groupIndex;
        }
        EXPECT_EQ(ordered.front().m_groupIndex, 0u);
        EXPECT_EQ(ordered.back().m_groupIndex, static_cast<uint32_t>(groups.size()) - 1);
        for (uint32_t index = 0; index < static_cast<uint32_t>(groups.size()); ++index)
        {
            EXPECT_EQ(groups[index].m_index, index);
            EXPECT_EQ(groups[index].m_maxDraws, draws[index]);
        }
    }
}
