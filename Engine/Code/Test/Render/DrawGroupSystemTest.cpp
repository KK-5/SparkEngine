#include <gtest/gtest.h>

#include <EASTL/vector.h>

#include <CoreComponents/Tags.h>

#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>

#include <DrawList/DrawGroupSystem.h>
#include <DrawList/DrawList.h>
#include <DrawList/DrawListKeyCounter.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/PassContext.h>
#include <PipelineState/PipelineKeyIndex.h>

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

    //! The views the test's passes render.
    struct TestViewTag
    {
    };
}

//! Groups made from the counted keys. Everything is components on the contexts, so no device is
//! needed; the indirect layout tables' buffers never materialize.
class DrawGroupSystemTest : public ::testing::Test
{
protected:
    RHI::RHIContext    rhi;
    PassContext        passes;
    DrawContext        draws;
    DrawGroupSystem    system;
    DrawListKeyCounter counter;
    //! The one view a test has unless it adds more.
    RHI::RHIHandle     view = AddView();

    RHI::RHIHandle AddView()
    {
        const RHI::RHIHandle added = rhi.CreateEntity();
        rhi.Add<View>(added);
        rhi.Add<TestViewTag>(added);
        return added;
    }

    //! A pass culling back faces, as the scene passes do, drawing the list of Tag under every
    //! TestViewTag view.
    template<typename Tag>
    Pass AddPass(const char* name)
    {
        PassPipelineState pipelineState;
        pipelineState.m_renderStates.m_rasterState.m_cullMode = RHI::CullMode::Back;

        PassCapabilities capabilities{};
        capabilities.m_collectViews = &CollectViews<TestViewTag>;

        const Pass pass = passes.CreatePass();
        passes.Add<PassName>(pass, PassName{ ObjectName(name) });
        passes.Add<PassPipelineState>(pass, pipelineState);
        passes.Add<PassCapabilities>(pass, capabilities);
        passes.Add<PassDrawList>(pass, PassDrawList{ DrawMaskBit<Tag>::Index, Tag::Order, Tag::Culling });
        return pass;
    }

    void Encode(PipelineKey key, DrawMask mask, uint32_t instances)
    {
        for (uint32_t i = 0; i < instances; ++i)
        {
            counter.Count(key, mask);
        }
    }

    //! A frame: the context emptied, what was encoded since the last one published, the groups made.
    void Frame()
    {
        draws.Clear();
        counter.Publish(draws);
        system.Update(rhi, passes, draws);
    }

    DrawGroupRange GroupsOf(Pass pass, RHI::RHIHandle under)
    {
        return DrawGroupsOf(draws, pass, under);
    }

    DrawGroupRange GroupsOf(Pass pass)
    {
        return GroupsOf(pass, view);
    }

    //! What the pass's indirect layout table holds for key, as uploaded.
    IndirectLayoutEntry Entry(Pass pass, PipelineKey key)
    {
        const auto& upload = rhi.Get<RHI::PendingBufferMap>(passes.Get<PassIndirectLayout>(pass).m_table);
        const auto* entries = static_cast<const IndirectLayoutEntry*>(upload.m_data);
        const uint32_t index = PipelineKeyIndexOf(key).m_value;
        if (index >= upload.m_byteCount / sizeof(IndirectLayoutEntry))
        {
            return IndirectLayoutEntry{};
        }
        return entries[index];
    }

    const DrawGroup* GroupOf(Pass pass, PipelineKey key)
    {
        for (const DrawGroup& group : GroupsOf(pass))
        {
            if (group.m_index == Entry(pass, key).m_countIndex)
            {
                return &group;
            }
        }
        return nullptr;
    }

    //! A translucent instance `under` sees, `depth` along it, as DrawListCullingSystem leaves one.
    void AddVisible(RHI::RHIHandle under, uint32_t slot, float depth, PipelineKey key)
    {
        VisibleInstance visible;
        visible.m_view             = under;
        visible.m_drawMaskBitIndex = DrawMaskBit<TranslucentTag>::Index;
        visible.m_slot             = slot;
        visible.m_key              = key;
        visible.m_depth            = depth;
        draws.Add<VisibleInstance>(draws.CreateEntity(), visible);
    }

    //! A frame of the visible instances added since the context was emptied.
    void CpuFrame()
    {
        SortVisibleInstances(draws);
        system.Update(rhi, passes, draws);
    }

    uint32_t ListKeyCount(DrawMask bit, PipelineKey key)
    {
        for (auto [entity, listKey] : draws.GetView<DrawListKey>().each())
        {
            if (listKey.m_drawMaskBitIndex == bit && listKey.m_key == key)
            {
                return listKey.m_instanceCount;
            }
        }
        return 0;
    }
};

TEST_F(DrawGroupSystemTest, AnInstanceCountsInTheListOfEachBitOfItsMask)
{
    Encode(SingleSided, Opaque | Caster, 2);
    Encode(SingleSided, Opaque, 1);
    Encode(DoubleSided, 0, 5);
    counter.Publish(draws);

    EXPECT_EQ(ListKeyCount(DrawMaskBit<OpaqueTag>::Index, SingleSided), 3u);
    EXPECT_EQ(ListKeyCount(DrawMaskBit<ShadowCasterTag>::Index, SingleSided), 2u);
    // Drawn nowhere, in no list: the culling never picks it.
    EXPECT_EQ(draws.GetView<DrawListKey>().size(), 2u);
}

//! A list the CPU culls is its instances, not its keys.
TEST_F(DrawGroupSystemTest, AListTheCpuCullsHasNoKeys)
{
    Encode(SingleSided, Opaque | DrawMaskOf<TranslucentTag>(), 2);
    counter.Publish(draws);

    EXPECT_EQ(ListKeyCount(DrawMaskBit<OpaqueTag>::Index, SingleSided), 2u);
    EXPECT_EQ(ListKeyCount(DrawMaskBit<TranslucentTag>::Index, SingleSided), 0u);
    EXPECT_EQ(draws.GetView<DrawListKey>().size(), 1u);
}

TEST_F(DrawGroupSystemTest, AListKeyCarriesTheKeyAndItsIndex)
{
    Encode(DoubleSided, Opaque, 1);
    counter.Publish(draws);

    ASSERT_EQ(draws.GetView<DrawListKey>().size(), 1u);
    const DrawListKey& listKey = draws.Get<DrawListKey>(*draws.GetView<DrawListKey>().begin());
    EXPECT_EQ(listKey.m_key, DoubleSided);
    EXPECT_EQ(listKey.m_keyIndex, PipelineKeyIndexOf(DoubleSided));
}

TEST_F(DrawGroupSystemTest, EachFrameIsCountedFromNothing)
{
    Encode(SingleSided, Opaque, 2);
    Frame();
    Frame();

    EXPECT_EQ(draws.GetView<DrawListKey>().size(), 0u);
}

//! Masking changes nothing in a template that does not declare it, and none does yet;
//! being double-sided changes the cull mode of a pass that culls.
TEST_F(DrawGroupSystemTest, KeysThatDrawAlikeShareAGroup)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Encode(SingleSided, Opaque, 1);
    Encode(Masked, Opaque, 1);
    Encode(DoubleSided, Opaque, 1);
    Frame();

    EXPECT_EQ(GroupsOf(depth).size(), 2u);
    EXPECT_EQ(Entry(depth, SingleSided).m_countIndex, Entry(depth, Masked).m_countIndex);
    EXPECT_NE(Entry(depth, SingleSided).m_countIndex, Entry(depth, DoubleSided).m_countIndex);
}

TEST_F(DrawGroupSystemTest, AGroupHoldsWhatItsKeysCountInThePassesList)
{
    const Pass depth  = AddPass<OpaqueTag>("Depth");
    const Pass shadow = AddPass<ShadowCasterTag>("Shadow");
    Encode(SingleSided, Opaque | Caster, 3);
    Encode(Masked, Opaque, 2);
    Encode(DoubleSided, Opaque | Caster, 4);
    Frame();

    EXPECT_EQ(GroupOf(depth, SingleSided)->m_maxDraws, 5u);
    EXPECT_EQ(GroupOf(depth, DoubleSided)->m_maxDraws, 4u);
    EXPECT_EQ(GroupOf(shadow, SingleSided)->m_maxDraws, 3u);
    EXPECT_EQ(GroupOf(shadow, DoubleSided)->m_maxDraws, 4u);

    // Next frame's counts replace this one's, and a key no instance has has no group.
    Encode(SingleSided, Opaque, 1);
    Frame();
    EXPECT_EQ(GroupsOf(depth).size(), 1u);
    EXPECT_EQ(GroupOf(depth, SingleSided)->m_maxDraws, 1u);
    EXPECT_EQ(GroupsOf(shadow).size(), 0u);
}

TEST_F(DrawGroupSystemTest, GroupsLieOneAfterAnotherInTheirIndexOrder)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Encode(SingleSided, Opaque, 3);
    Encode(DoubleSided, Opaque, 4);
    Frame();

    const DrawGroupRange groups = GroupsOf(depth);
    ASSERT_EQ(groups.size(), 2u);
    uint32_t firstDraw = 0;
    for (uint32_t i = 0; i < groups.size(); ++i)
    {
        EXPECT_EQ(groups[i].m_index, i);
        EXPECT_EQ(groups[i].m_firstDraw, firstDraw);
        EXPECT_EQ(groups[i].m_view, view);
        firstDraw += groups[i].m_maxDraws;
    }
    EXPECT_EQ(firstDraw, 7u);
}

//! Which instances a view sees only the GPU knows, so each view gets the groups of the whole list.
TEST_F(DrawGroupSystemTest, EachViewHasTheSameGroups)
{
    const RHI::RHIHandle other = AddView();
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Encode(SingleSided, Opaque, 3);
    Encode(DoubleSided, Opaque, 4);
    Frame();

    const DrawGroupRange groups      = GroupsOf(depth, view);
    const DrawGroupRange otherGroups = GroupsOf(depth, other);
    ASSERT_EQ(groups.size(), 2u);
    ASSERT_EQ(otherGroups.size(), groups.size());
    for (uint32_t i = 0; i < groups.size(); ++i)
    {
        EXPECT_EQ(groups[i].m_view, view);
        EXPECT_EQ(otherGroups[i].m_view, other);
        EXPECT_EQ(otherGroups[i].m_index, groups[i].m_index);
        EXPECT_EQ(otherGroups[i].m_variant, groups[i].m_variant);
        EXPECT_EQ(otherGroups[i].m_firstDraw, groups[i].m_firstDraw);
        EXPECT_EQ(otherGroups[i].m_maxDraws, groups[i].m_maxDraws);
    }
}

//! The binning pass reads the table and the layout every frame, views or none.
TEST_F(DrawGroupSystemTest, APassUnderNoViewHasATableButNoGroups)
{
    rhi.Add<DeadTag>(view);
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Encode(SingleSided, Opaque, 3);
    Frame();

    EXPECT_EQ(draws.GetView<DrawGroup>().size(), 0u);
    EXPECT_TRUE(passes.Has<PassIndirectLayout>(depth));
    EXPECT_NE(Entry(depth, SingleSided).m_countIndex, IndirectLayoutEntry::NotDrawn);
}

//! However the groups were added, each pass's under each view are a run of their own.
TEST_F(DrawGroupSystemTest, APassUnderAViewHasItsOwnRunOfGroupsInOrder)
{
    const RHI::RHIHandle other = AddView();
    const Pass depth  = AddPass<OpaqueTag>("Depth");
    const Pass shadow = AddPass<ShadowCasterTag>("Shadow");
    Encode(SingleSided, Opaque | Caster, 3);
    Encode(DoubleSided, Opaque, 4);
    Frame();

    for (Pass pass : { depth, shadow })
    {
        for (RHI::RHIHandle under : { view, other })
        {
            const DrawGroupRange groups = GroupsOf(pass, under);
            ASSERT_EQ(groups.size(), pass == depth ? 2u : 1u);
            for (uint32_t i = 0; i < groups.size(); ++i)
            {
                EXPECT_EQ(groups[i].m_pass, pass);
                EXPECT_EQ(groups[i].m_view, under);
                EXPECT_EQ(groups[i].m_index, i);
            }
        }
    }
    EXPECT_EQ(draws.GetView<DrawGroup>().size(), 6u);
}

TEST_F(DrawGroupSystemTest, NoGroupsIsAnEmptyRun)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    // Before any frame there is not even a storage of groups.
    EXPECT_TRUE(GroupsOf(depth).empty());

    Frame();
    EXPECT_TRUE(GroupsOf(depth).empty());
}

//! A list the CPU culls draws in the order it is sorted, cut where the variant changes.
TEST_F(DrawGroupSystemTest, AListTheCpuCullsIsCutWhereItsVariantChanges)
{
    const Pass translucency = AddPass<TranslucentTag>("Translucency");
    AddVisible(view, 0, 50.0f, SingleSided);
    AddVisible(view, 1, 40.0f, SingleSided);
    AddVisible(view, 2, 30.0f, DoubleSided);
    AddVisible(view, 3, 20.0f, SingleSided);
    CpuFrame();

    const DrawGroupRange groups = GroupsOf(translucency);
    ASSERT_EQ(groups.size(), 3u);
    EXPECT_EQ(groups[0].m_maxDraws, 2u);
    EXPECT_EQ(groups[1].m_maxDraws, 1u);
    EXPECT_EQ(groups[2].m_maxDraws, 1u);
    EXPECT_EQ(groups[1].m_variant.m_cullMode, RHI::CullMode::None);
    uint32_t firstDraw = 0;
    for (uint32_t i = 0; i < groups.size(); ++i)
    {
        EXPECT_EQ(groups[i].m_index, i);
        EXPECT_EQ(groups[i].m_firstDraw, firstDraw);
        firstDraw += groups[i].m_maxDraws;
    }
}

//! Every view's draws lie in one buffer, so a view's groups start where its draws do.
TEST_F(DrawGroupSystemTest, AViewsGroupsStartWhereItsDrawsDo)
{
    const RHI::RHIHandle other = AddView();
    const Pass translucency = AddPass<TranslucentTag>("Translucency");
    AddVisible(view, 0, 20.0f, SingleSided);
    AddVisible(view, 1, 10.0f, SingleSided);
    AddVisible(other, 2, 30.0f, SingleSided);
    AddVisible(other, 3, 20.0f, DoubleSided);
    CpuFrame();

    for (RHI::RHIHandle under : { view, other })
    {
        const DrawGroupRange groups = GroupsOf(translucency, under);
        ASSERT_FALSE(groups.empty());
        EXPECT_EQ(groups[0].m_firstDraw, FirstDrawOffset(draws, VisibleInstancesOf(draws, under, DrawMaskBit<TranslucentTag>::Index)));
    }
    EXPECT_EQ(GroupsOf(translucency, view).size(), 1u);
    EXPECT_EQ(GroupsOf(translucency, other).size(), 2u);
    EXPECT_EQ(GroupsOf(translucency, other)[1].m_firstDraw, GroupsOf(translucency, other)[0].m_firstDraw + 1);
}

//! Its draws are the list's, written by DrawListCullingSystem: the pass has no table of its own.
TEST_F(DrawGroupSystemTest, APassOfAListTheCpuCullsHasNoIndirectLayout)
{
    const Pass translucency = AddPass<TranslucentTag>("Translucency");
    AddVisible(view, 0, 20.0f, SingleSided);
    CpuFrame();

    EXPECT_EQ(GroupsOf(translucency).size(), 1u);
    EXPECT_FALSE(passes.Has<PassIndirectLayout>(translucency));
}

TEST_F(DrawGroupSystemTest, ALayoutRoundsUpAStepAtATime)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Frame();
    EXPECT_EQ(passes.Get<PassIndirectLayout>(depth).m_countsPerView, 4u);
    EXPECT_EQ(passes.Get<PassIndirectLayout>(depth).m_recordsPerView, 1024u);

    Encode(SingleSided, Opaque, 1000);
    Encode(DoubleSided, Opaque, 25);
    Frame();
    EXPECT_EQ(passes.Get<PassIndirectLayout>(depth).m_countsPerView, 4u);
    EXPECT_EQ(passes.Get<PassIndirectLayout>(depth).m_recordsPerView, 2048u);
}

TEST_F(DrawGroupSystemTest, ATableSendsAKeyToItsGroupAndAKeyOfNoGroupNowhere)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Encode(SingleSided, Opaque, 3);
    Encode(DoubleSided, Opaque, 4);
    Frame();

    for (const DrawGroup& group : GroupsOf(depth))
    {
        const PipelineKey key = group.m_variant.m_cullMode == RHI::CullMode::None ? DoubleSided : SingleSided;
        EXPECT_EQ(Entry(depth, key).m_countIndex, group.m_index);
        EXPECT_EQ(Entry(depth, key).m_firstRecord, group.m_firstDraw);
    }
    EXPECT_EQ(Entry(depth, Masked).m_countIndex, IndirectLayoutEntry::NotDrawn);
}

TEST_F(DrawGroupSystemTest, ATableIsAsLongAsTheKeysItCanBeAskedFor)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Encode(DoubleSided, Opaque, 1);
    Frame();

    EXPECT_EQ(passes.Get<PassIndirectLayout>(depth).m_tableLength, PipelineKeyIndexOf(DoubleSided).m_value + 1);
}

TEST_F(DrawGroupSystemTest, APassDrawingNoListHasNoGroups)
{
    const Pass post = passes.CreatePass();
    passes.Add<PassName>(post, PassName{ ObjectName("Post") });
    Encode(SingleSided, Opaque, 1);
    Frame();

    EXPECT_FALSE(passes.Has<PassIndirectLayout>(post));
    EXPECT_EQ(GroupsOf(post).size(), 0u);
}

TEST_F(DrawGroupSystemTest, ShutdownLeavesNothingBehind)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Encode(SingleSided, Opaque, 1);
    Frame();
    const RHI::RHIHandle buffer = passes.Get<PassIndirectLayout>(depth).m_table;

    system.Shutdown(rhi, passes);

    EXPECT_TRUE(rhi.Has<DeadTag>(buffer));
    EXPECT_FALSE(passes.Has<PassIndirectLayout>(depth));
    EXPECT_FALSE(passes.Has<PassIndirectLayout>(depth));
}
