#include <gtest/gtest.h>

#include <EASTL/sort.h>
#include <EASTL/vector.h>

#include <CoreComponents/Tags.h>

#include <RHI/Context/RHIContext.h>

#include <DrawBin/DrawBinSystem.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/PassContext.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    constexpr DrawMask Opaque = DrawMaskOf<OpaqueTag>();
    constexpr DrawMask Caster = DrawMaskOf<ShadowCasterTag>();

    PipelineKey Key(Resource::AlphaMode alphaMode, bool doubleSided)
    {
        Resource::MaterialState state;
        state.m_alphaMode   = alphaMode;
        state.m_doubleSided = doubleSided;
        return MakePipelineKey(0, state);
    }

    const PipelineKey SingleSided = Key(Resource::AlphaMode::Opaque, false);
    const PipelineKey DoubleSided = Key(Resource::AlphaMode::Opaque, true);
    const PipelineKey Masked      = Key(Resource::AlphaMode::Mask, false);
}

//! Bins kept from published counts. Everything is components on the two contexts, so no
//! device is needed; the bin tables' buffers never materialize and are not uploaded.
class DrawBinTest : public ::testing::Test
{
protected:
    RHI::RHIContext rhi;
    PassContext     passes;
    DrawBinSystem   system;
    KeyInstanceCounts counts{};

    //! A pass culling back faces, as the scene passes do, drawing the list of bitIndex.
    Pass AddPass(const char* name, uint32_t bitIndex)
    {
        PassPipelineState pipelineState;
        pipelineState.m_renderStates.m_rasterState.m_cullMode = RHI::CullMode::Back;

        const Pass pass = passes.CreatePass();
        passes.Add<PassName>(pass, PassName{ ObjectName(name) });
        passes.Add<PassPipelineState>(pass, pipelineState);
        passes.Add<PassDrawList>(pass, PassDrawList{ bitIndex });
        return pass;
    }

    void Encode(PipelineKey key, DrawMask mask, uint32_t instances)
    {
        for (uint32_t i = 0; i < instances; ++i)
        {
            CountKeyInstance(counts, key, mask);
        }
    }

    //! A frame: what was encoded since the last one is published, then the bins kept.
    void Frame()
    {
        PublishKeyInstanceCounts(rhi, counts);
        system.Update(rhi, passes);
        counts.fill(KeyInstanceCount{});
    }

    eastl::vector<DrawBinHandle> BinsOf(Pass pass)
    {
        eastl::vector<DrawBinHandle> bins;
        for (auto [bin, of] : passes.GetView<DrawBinOf>().each())
        {
            if (of.m_pass == pass)
            {
                bins.push_back(bin);
            }
        }
        return bins;
    }

    size_t AssignmentCount()
    {
        return passes.GetView<DrawBinAssignment>().size();
    }

    const DrawBinTableEntry& Entry(Pass pass, PipelineKey key)
    {
        return passes.Get<PassBinTable>(pass).m_entries[key.m_value];
    }

    const DrawBinRange& RangeOf(Pass pass, PipelineKey key)
    {
        for (DrawBinHandle bin : BinsOf(pass))
        {
            if (passes.Get<DrawBinRange>(bin).m_index == Entry(pass, key).m_index)
            {
                return passes.Get<DrawBinRange>(bin);
            }
        }
        ADD_FAILURE() << "key " << key.m_value << " has no bin";
        static DrawBinRange none;
        return none;
    }
};

TEST_F(DrawBinTest, AnInstanceCountsUnderEachBitOfItsMask)
{
    Encode(SingleSided, Opaque | Caster, 2);
    Encode(SingleSided, Opaque, 1);
    Encode(DoubleSided, 0, 5);

    EXPECT_EQ(counts[SingleSided.m_value].m_counts[DrawMaskBit<OpaqueTag>::Index], 3u);
    EXPECT_EQ(counts[SingleSided.m_value].m_counts[DrawMaskBit<ShadowCasterTag>::Index], 2u);
    // Drawn nowhere, counted nowhere: no list the culling makes has it.
    EXPECT_EQ(counts[DoubleSided.m_value].m_counts[DrawMaskBit<OpaqueTag>::Index], 0u);
    EXPECT_EQ(counts[DoubleSided.m_value].m_counts[DrawMaskBit<ShadowCasterTag>::Index], 0u);
}

TEST_F(DrawBinTest, AKeyGetsAnEntityTheFirstTimeItIsCountedAndKeepsIt)
{
    Encode(DoubleSided, 0, 1);
    PublishKeyInstanceCounts(rhi, counts);
    EXPECT_EQ(rhi.GetView<PipelineKey>().size(), 0u);

    Encode(SingleSided, Opaque, 2);
    PublishKeyInstanceCounts(rhi, counts);
    ASSERT_EQ(rhi.GetView<PipelineKey>().size(), 1u);
    const RHI::RHIHandle key = *rhi.GetView<PipelineKey>().begin();
    EXPECT_EQ(rhi.Get<PipelineKey>(key), SingleSided);
    EXPECT_EQ(rhi.Get<KeyInstanceCount>(key).m_counts[DrawMaskBit<OpaqueTag>::Index], 2u);

    counts.fill(KeyInstanceCount{});
    PublishKeyInstanceCounts(rhi, counts);
    ASSERT_EQ(rhi.GetView<PipelineKey>().size(), 1u);
    EXPECT_EQ(rhi.Get<KeyInstanceCount>(key).m_counts[DrawMaskBit<OpaqueTag>::Index], 0u);
}

//! Masking changes nothing in a template that does not declare it, and none does yet;
//! being double-sided changes the cull mode of a pass that culls.
TEST_F(DrawBinTest, KeysThatDrawAlikeShareABin)
{
    const Pass depth = AddPass("Depth", DrawMaskBit<OpaqueTag>::Index);
    Encode(SingleSided, Opaque, 1);
    Encode(Masked, Opaque, 1);
    Encode(DoubleSided, Opaque, 1);
    Frame();

    EXPECT_EQ(BinsOf(depth).size(), 2u);
    EXPECT_EQ(AssignmentCount(), 3u);
    EXPECT_EQ(Entry(depth, SingleSided).m_index, Entry(depth, Masked).m_index);
    EXPECT_NE(Entry(depth, SingleSided).m_index, Entry(depth, DoubleSided).m_index);
}

TEST_F(DrawBinTest, ABinHoldsWhatItsKeysCountInThePassesList)
{
    const Pass depth  = AddPass("Depth", DrawMaskBit<OpaqueTag>::Index);
    const Pass shadow = AddPass("Shadow", DrawMaskBit<ShadowCasterTag>::Index);
    Encode(SingleSided, Opaque | Caster, 3);
    Encode(Masked, Opaque, 2);
    Encode(DoubleSided, Opaque | Caster, 4);
    Frame();

    EXPECT_EQ(RangeOf(depth, SingleSided).m_capacity, 5u);
    EXPECT_EQ(RangeOf(depth, DoubleSided).m_capacity, 4u);
    EXPECT_EQ(RangeOf(shadow, SingleSided).m_capacity, 3u);
    EXPECT_EQ(RangeOf(shadow, DoubleSided).m_capacity, 4u);

    // Next frame's counts replace this one's.
    Encode(SingleSided, Opaque, 1);
    Frame();
    EXPECT_EQ(RangeOf(depth, SingleSided).m_capacity, 1u);
    EXPECT_EQ(RangeOf(depth, DoubleSided).m_capacity, 0u);
    EXPECT_EQ(RangeOf(shadow, SingleSided).m_capacity, 0u);
}

TEST_F(DrawBinTest, BinsLieOneAfterAnotherInTheirIndexOrder)
{
    const Pass depth = AddPass("Depth", DrawMaskBit<OpaqueTag>::Index);
    Encode(SingleSided, Opaque, 3);
    Encode(DoubleSided, Opaque, 4);
    Frame();

    eastl::vector<DrawBinRange> ranges;
    for (DrawBinHandle bin : BinsOf(depth))
    {
        ranges.push_back(passes.Get<DrawBinRange>(bin));
    }
    eastl::sort(ranges.begin(), ranges.end(),
        [](const DrawBinRange& a, const DrawBinRange& b) { return a.m_index < b.m_index; });

    uint32_t first = 0;
    for (uint32_t i = 0; i < ranges.size(); ++i)
    {
        EXPECT_EQ(ranges[i].m_index, i);
        EXPECT_EQ(ranges[i].m_first, first);
        first += ranges[i].m_capacity;
    }
    EXPECT_EQ(first, 7u);
}

TEST_F(DrawBinTest, ALaterPriorityDrawsLater)
{
    const Pass depth = AddPass("Depth", DrawMaskBit<OpaqueTag>::Index);
    Encode(SingleSided, Opaque, 1);
    Encode(DoubleSided, Opaque, 1);
    Frame();

    // No rule raises a priority yet; raise one by hand, as masking will in a template that clips.
    for (DrawBinHandle bin : BinsOf(depth))
    {
        if (passes.Get<DrawBinRange>(bin).m_index == Entry(depth, SingleSided).m_index)
        {
            passes.Get<PipelineVariant>(bin).m_priority = 1;
        }
    }
    Encode(SingleSided, Opaque, 1);
    Encode(DoubleSided, Opaque, 1);
    Frame();

    EXPECT_EQ(Entry(depth, DoubleSided).m_index, 0u);
    EXPECT_EQ(Entry(depth, SingleSided).m_index, 1u);
    EXPECT_EQ(Entry(depth, SingleSided).m_first, 1u);
}

TEST_F(DrawBinTest, ALayoutRoundsUpAStepAtATime)
{
    const Pass depth = AddPass("Depth", DrawMaskBit<OpaqueTag>::Index);
    Frame();
    EXPECT_EQ(passes.Get<PassBinLayout>(depth).m_binCount, 4u);
    EXPECT_EQ(passes.Get<PassBinLayout>(depth).m_viewStride, 1024u);

    Encode(SingleSided, Opaque, 1000);
    Encode(DoubleSided, Opaque, 25);
    Frame();
    EXPECT_EQ(passes.Get<PassBinLayout>(depth).m_binCount, 4u);
    EXPECT_EQ(passes.Get<PassBinLayout>(depth).m_viewStride, 2048u);
}

TEST_F(DrawBinTest, ATableSendsAKeyToItsBinAndAnUnassignedKeyNowhere)
{
    const Pass depth = AddPass("Depth", DrawMaskBit<OpaqueTag>::Index);
    Encode(SingleSided, Opaque, 3);
    Encode(DoubleSided, Opaque, 4);
    Frame();

    for (DrawBinHandle bin : BinsOf(depth))
    {
        const auto& range = passes.Get<DrawBinRange>(bin);
        const PipelineKey key = passes.Get<PipelineVariant>(bin).m_cullMode == RHI::CullMode::None ? DoubleSided : SingleSided;
        EXPECT_EQ(Entry(depth, key).m_index, range.m_index);
        EXPECT_EQ(Entry(depth, key).m_first, range.m_first);
    }
    EXPECT_EQ(Entry(depth, Masked).m_index, DrawBinTableEntry::NotDrawn);
}

//! A key and a pass are assigned once each way round, whichever came first.
TEST_F(DrawBinTest, ANewPassAndANewKeyAreEachAssignedOnce)
{
    const Pass depth = AddPass("Depth", DrawMaskBit<OpaqueTag>::Index);
    Encode(SingleSided, Opaque, 1);
    Frame();
    EXPECT_EQ(AssignmentCount(), 1u);

    const Pass shadow = AddPass("Shadow", DrawMaskBit<ShadowCasterTag>::Index);
    Encode(DoubleSided, Opaque, 1);
    Frame();
    EXPECT_EQ(AssignmentCount(), 4u);
    EXPECT_NE(Entry(shadow, SingleSided).m_index, DrawBinTableEntry::NotDrawn);
    EXPECT_NE(Entry(shadow, DoubleSided).m_index, DrawBinTableEntry::NotDrawn);
    EXPECT_NE(Entry(depth, DoubleSided).m_index, DrawBinTableEntry::NotDrawn);

    Frame();
    EXPECT_EQ(AssignmentCount(), 4u);
}

TEST_F(DrawBinTest, APassDrawingNoListHasNoBins)
{
    const Pass post = passes.CreatePass();
    passes.Add<PassName>(post, PassName{ ObjectName("Post") });
    Encode(SingleSided, Opaque, 1);
    Frame();

    EXPECT_FALSE(passes.Has<PassBinTable>(post));
    EXPECT_EQ(AssignmentCount(), 0u);
}

TEST_F(DrawBinTest, ShutdownLeavesNothingBehind)
{
    const Pass depth = AddPass("Depth", DrawMaskBit<OpaqueTag>::Index);
    Encode(SingleSided, Opaque, 1);
    Frame();
    const RHI::RHIHandle buffer = passes.Get<PassBinTable>(depth).m_buffer;

    system.Shutdown(rhi, passes);

    EXPECT_TRUE(rhi.Has<DeadTag>(buffer));
    EXPECT_FALSE(passes.Has<PassBinTable>(depth));
    EXPECT_EQ(passes.GetView<DrawBinOf>().size(), 0u);
    EXPECT_EQ(AssignmentCount(), 0u);
    for (auto [key, value] : rhi.GetView<PipelineKey>().each())
    {
        EXPECT_TRUE(rhi.Has<DeadTag>(key));
    }
}
