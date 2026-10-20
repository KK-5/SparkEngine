#include <gtest/gtest.h>

#include <EASTL/vector.h>

#include <CoreComponents/Tags.h>

#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>

#include <DrawBin/DrawBinSystem.h>
#include <DrawBin/DrawListKeyCounter.h>
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
}

//! Bins made from the counted keys. Everything is components on the contexts, so no device is
//! needed; the bins get no PSO and the bin tables' buffers never materialize.
class DrawBinTest : public ::testing::Test
{
protected:
    RHI::RHIContext    rhi;
    PassContext        passes;
    DrawContext        draws;
    DrawBinSystem      system;
    DrawListKeyCounter counter;

    //! A pass culling back faces, as the scene passes do, drawing the list of Tag.
    template<typename Tag>
    Pass AddPass(const char* name)
    {
        PassPipelineState pipelineState;
        pipelineState.m_renderStates.m_rasterState.m_cullMode = RHI::CullMode::Back;

        const Pass pass = passes.CreatePass();
        passes.Add<PassName>(pass, PassName{ ObjectName(name) });
        passes.Add<PassPipelineState>(pass, pipelineState);
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

    //! A frame: the context emptied, what was encoded since the last one published, the bins made.
    void Frame()
    {
        draws.Clear();
        counter.Publish(draws);
        system.Update(rhi, passes, draws);
    }

    DrawBinList BinsOf(Pass pass)
    {
        DrawBinList bins;
        CollectDrawBins(draws, pass, RHI::NullHandle, bins);
        return bins;
    }

    //! What the pass's bin table holds for key, as uploaded.
    DrawBinTableEntry Entry(Pass pass, PipelineKey key)
    {
        const auto& upload = rhi.Get<RHI::PendingBufferMap>(passes.Get<PassBinTable>(pass).m_buffer);
        const auto* entries = static_cast<const DrawBinTableEntry*>(upload.m_data);
        const uint32_t index = PipelineKeyIndexOf(key).m_value;
        if (index >= upload.m_byteCount / sizeof(DrawBinTableEntry))
        {
            return DrawBinTableEntry{};
        }
        return entries[index];
    }

    const DrawBin* BinOf(Pass pass, PipelineKey key)
    {
        for (const DrawBin* bin : BinsOf(pass))
        {
            if (bin->m_index == Entry(pass, key).m_index)
            {
                return bin;
            }
        }
        return nullptr;
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

TEST_F(DrawBinTest, AnInstanceCountsInTheListOfEachBitOfItsMask)
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

TEST_F(DrawBinTest, AListKeyCarriesTheKeyAndItsIndex)
{
    Encode(DoubleSided, Opaque, 1);
    counter.Publish(draws);

    ASSERT_EQ(draws.GetView<DrawListKey>().size(), 1u);
    const DrawListKey& listKey = draws.Get<DrawListKey>(*draws.GetView<DrawListKey>().begin());
    EXPECT_EQ(listKey.m_key, DoubleSided);
    EXPECT_EQ(listKey.m_keyIndex, PipelineKeyIndexOf(DoubleSided));
}

TEST_F(DrawBinTest, EachFrameIsCountedFromNothing)
{
    Encode(SingleSided, Opaque, 2);
    Frame();
    Frame();

    EXPECT_EQ(draws.GetView<DrawListKey>().size(), 0u);
}

//! Masking changes nothing in a template that does not declare it, and none does yet;
//! being double-sided changes the cull mode of a pass that culls.
TEST_F(DrawBinTest, KeysThatDrawAlikeShareABin)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Encode(SingleSided, Opaque, 1);
    Encode(Masked, Opaque, 1);
    Encode(DoubleSided, Opaque, 1);
    Frame();

    EXPECT_EQ(BinsOf(depth).size(), 2u);
    EXPECT_EQ(Entry(depth, SingleSided).m_index, Entry(depth, Masked).m_index);
    EXPECT_NE(Entry(depth, SingleSided).m_index, Entry(depth, DoubleSided).m_index);
}

TEST_F(DrawBinTest, ABinHoldsWhatItsKeysCountInThePassesList)
{
    const Pass depth  = AddPass<OpaqueTag>("Depth");
    const Pass shadow = AddPass<ShadowCasterTag>("Shadow");
    Encode(SingleSided, Opaque | Caster, 3);
    Encode(Masked, Opaque, 2);
    Encode(DoubleSided, Opaque | Caster, 4);
    Frame();

    EXPECT_EQ(BinOf(depth, SingleSided)->m_maxDraws, 5u);
    EXPECT_EQ(BinOf(depth, DoubleSided)->m_maxDraws, 4u);
    EXPECT_EQ(BinOf(shadow, SingleSided)->m_maxDraws, 3u);
    EXPECT_EQ(BinOf(shadow, DoubleSided)->m_maxDraws, 4u);

    // Next frame's counts replace this one's, and a key no instance has has no bin.
    Encode(SingleSided, Opaque, 1);
    Frame();
    EXPECT_EQ(BinsOf(depth).size(), 1u);
    EXPECT_EQ(BinOf(depth, SingleSided)->m_maxDraws, 1u);
    EXPECT_EQ(BinsOf(shadow).size(), 0u);
}

TEST_F(DrawBinTest, BinsLieOneAfterAnotherInTheirIndexOrder)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Encode(SingleSided, Opaque, 3);
    Encode(DoubleSided, Opaque, 4);
    Frame();

    const DrawBinList bins = BinsOf(depth);
    ASSERT_EQ(bins.size(), 2u);
    uint32_t firstDraw = 0;
    for (uint32_t i = 0; i < bins.size(); ++i)
    {
        EXPECT_EQ(bins[i]->m_index, i);
        EXPECT_EQ(bins[i]->m_firstDraw, firstDraw);
        EXPECT_EQ(bins[i]->m_view, RHI::NullHandle);
        firstDraw += bins[i]->m_maxDraws;
    }
    EXPECT_EQ(firstDraw, 7u);
}

TEST_F(DrawBinTest, ALayoutRoundsUpAStepAtATime)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Frame();
    EXPECT_EQ(passes.Get<PassBinLayout>(depth).m_binCount, 4u);
    EXPECT_EQ(passes.Get<PassBinLayout>(depth).m_viewStride, 1024u);

    Encode(SingleSided, Opaque, 1000);
    Encode(DoubleSided, Opaque, 25);
    Frame();
    EXPECT_EQ(passes.Get<PassBinLayout>(depth).m_binCount, 4u);
    EXPECT_EQ(passes.Get<PassBinLayout>(depth).m_viewStride, 2048u);
}

TEST_F(DrawBinTest, ATableSendsAKeyToItsBinAndAKeyOfNoBinNowhere)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Encode(SingleSided, Opaque, 3);
    Encode(DoubleSided, Opaque, 4);
    Frame();

    for (const DrawBin* bin : BinsOf(depth))
    {
        const PipelineKey key = bin->m_variant.m_cullMode == RHI::CullMode::None ? DoubleSided : SingleSided;
        EXPECT_EQ(Entry(depth, key).m_index, bin->m_index);
        EXPECT_EQ(Entry(depth, key).m_first, bin->m_firstDraw);
    }
    EXPECT_EQ(Entry(depth, Masked).m_index, DrawBinTableEntry::NotDrawn);
}

TEST_F(DrawBinTest, ATableIsAsLongAsTheKeysItCanBeAskedFor)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Encode(DoubleSided, Opaque, 1);
    Frame();

    const PassBinTable& table = passes.Get<PassBinTable>(depth);
    EXPECT_EQ(table.m_entryCount, PipelineKeyIndexOf(DoubleSided).m_value + 1);
    EXPECT_GE(table.m_capacity, table.m_entryCount);
}

TEST_F(DrawBinTest, APassDrawingNoListHasNoBins)
{
    const Pass post = passes.CreatePass();
    passes.Add<PassName>(post, PassName{ ObjectName("Post") });
    Encode(SingleSided, Opaque, 1);
    Frame();

    EXPECT_FALSE(passes.Has<PassBinTable>(post));
    EXPECT_EQ(BinsOf(post).size(), 0u);
}

TEST_F(DrawBinTest, ShutdownLeavesNothingBehind)
{
    const Pass depth = AddPass<OpaqueTag>("Depth");
    Encode(SingleSided, Opaque, 1);
    Frame();
    const RHI::RHIHandle buffer = passes.Get<PassBinTable>(depth).m_buffer;

    system.Shutdown(rhi, passes);

    EXPECT_TRUE(rhi.Has<DeadTag>(buffer));
    EXPECT_FALSE(passes.Has<PassBinTable>(depth));
    EXPECT_FALSE(passes.Has<PassBinLayout>(depth));
}
