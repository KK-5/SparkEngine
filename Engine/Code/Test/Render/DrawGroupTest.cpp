#include <gtest/gtest.h>

#include <EASTL/algorithm.h>
#include <EASTL/vector.h>

#include <DrawBin/DrawGroup.h>

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
    const DrawGroup* merged = groups[0].m_variant == Default ? &groups[0] : &groups[1];
    EXPECT_EQ(merged->m_entryCount, 2u);
    EXPECT_EQ(merged->m_maxDraws, 8u);

    // Both keys are still to be found among the group's entries.
    eastl::vector<uint32_t> keys;
    for (uint32_t e = merged->m_firstEntry; e < merged->m_firstEntry + merged->m_entryCount; ++e)
    {
        EXPECT_EQ(entries[e].m_variant, Default);
        keys.push_back(entries[e].m_keyIndex);
    }
    EXPECT_NE(eastl::find(keys.begin(), keys.end(), 7u), keys.end());
    EXPECT_NE(eastl::find(keys.begin(), keys.end(), 9u), keys.end());
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
    EXPECT_EQ(groups[1].m_entryCount, 2u);
    EXPECT_EQ(groups[2].m_variant, Later);
    EXPECT_EQ(entries[0].m_variant, Later);
    EXPECT_EQ(entries[3].m_variant, Later);
}

TEST(DrawGroupTest, TheGroupsCoverEveryEntryOnceInOrder)
{
    eastl::vector<DrawEntry> entries{
        Entry(Later, 1), Entry(TwoSided, 1), Entry(Default, 1), Entry(TwoSided, 1), Entry(Default, 1) };

    for (DrawOrder order : { DrawOrder::Any, DrawOrder::BackToFront })
    {
        eastl::vector<DrawEntry> ordered = entries;
        eastl::vector<DrawGroup> groups;

        GroupDrawEntries(ordered, order, groups);

        uint32_t next = 0;
        for (const DrawGroup& group : groups)
        {
            EXPECT_EQ(group.m_firstEntry, next);
            EXPECT_GT(group.m_entryCount, 0u);
            next += group.m_entryCount;
        }
        EXPECT_EQ(next, static_cast<uint32_t>(entries.size()));
    }
}
