#include <gtest/gtest.h>

#include <EASTL/sort.h>
#include <EASTL/vector.h>

#include <Math/Frustum.h>
#include <Math/MathUtils.h>

#include <CoreComponents/Tags.h>

#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>

#include <DrawList/DrawList.h>
#include <DrawList/DrawListCullingSystem.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/PassDrawListComponents.h>
#include <Pass/PassCapabilities.h>
#include <Pass/PassContext.h>
#include <View/View.h>
#include <View/ViewComponents.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    constexpr uint32_t TranslucentBit = DrawMaskBit<TranslucentTag>::Index;

    //! The views the test's passes render.
    struct TestViewTag
    {
    };

    PipelineKey Key(bool doubleSided)
    {
        Resource::MaterialState state;
        state.m_doubleSided = doubleSided;
        return MakePipelineKey(0, state);
    }
}

//! Each view's instances of the lists the CPU culls. A view looks down +z from the origin,
//! seeing 45 degrees to each side, from 1 to 100 away.
class DrawListCullingSystemTest : public ::testing::Test
{
protected:
    RHI::RHIContext       rhi;
    PassContext           passes;
    DrawContext           draws;
    DrawListCullingSystem system;
    RHI::RHIHandle        view = AddView(true);

    RHI::RHIHandle AddView(bool drawn)
    {
        const View made = MakePerspectiveView(Math::Vector3(0.0f), Math::Vector3(0.0f, 0.0f, 1.0f),
            Math::Vector3(0.0f, 1.0f, 0.0f), Math::Radians(90.0f), 1.0f, 1.0f, 100.0f);

        const RHI::RHIHandle added = rhi.CreateEntity();
        rhi.Add<View>(added, made);
        rhi.Add<ViewFrustum>(added, ViewFrustum{ Math::Frustum::FromViewProjection(made.GetWorldToClip()) });
        if (drawn)
        {
            rhi.Add<TestViewTag>(added);
        }
        return added;
    }

    template<typename Tag>
    Pass AddPass(const char* name)
    {
        PassCapabilities capabilities{};
        capabilities.m_collectViews = &CollectViews<TestViewTag>;

        const Pass pass = passes.CreatePass();
        passes.Add<PassName>(pass, PassName{ ObjectName(name) });
        passes.Add<PassCapabilities>(pass, capabilities);
        passes.Add<PassDrawList>(pass, PassDrawList{ DrawMaskBit<Tag>::Index, Tag::Order, Tag::Culling });
        return pass;
    }

    //! A translucent instance of a box of half-size `half` around `center`.
    void AddInstance(uint32_t position, const Math::Vector3& center, float half = 0.5f, PipelineKey key = Key(false))
    {
        DrawListInstance instance;
        instance.m_drawMaskBitIndex = TranslucentBit;
        instance.m_position             = position;
        instance.m_key              = key;
        instance.m_indexCount       = 36;
        instance.m_firstIndex       = 6 * position;
        instance.m_vertexOffset     = 24;
        instance.m_worldBounds.min  = center - Math::Vector3(half);
        instance.m_worldBounds.max  = center + Math::Vector3(half);
        draws.Add<DrawListInstance>(draws.CreateEntity(), instance);
    }

    //! The CpuCulledDrawArgumentsTag buffers there are.
    eastl::vector<RHI::RHIHandle> DrawArgumentsBuffers()
    {
        eastl::vector<RHI::RHIHandle> buffers;
        rhi.GetView<CpuCulledDrawArgumentsTag>().each([&](RHI::RHIHandle buffer) { buffers.push_back(buffer); });
        return buffers;
    }

    //! The draws as uploaded this frame.
    eastl::vector<RHI::DrawIndexedIndirectCommand> UploadedDraws()
    {
        const eastl::vector<RHI::RHIHandle> buffers = DrawArgumentsBuffers();
        eastl::vector<RHI::DrawIndexedIndirectCommand> draws;
        for (RHI::RHIHandle buffer : buffers)
        {
            if (const auto* upload = rhi.TryGet<RHI::PendingBufferMap>(buffer))
            {
                const auto* commands = static_cast<const RHI::DrawIndexedIndirectCommand*>(upload->m_data);
                draws.assign(commands, commands + upload->m_byteCount / sizeof(RHI::DrawIndexedIndirectCommand));
            }
        }
        return draws;
    }
    //! The positions under `under`, in the order they draw.
    eastl::vector<uint32_t> PositionsOf(RHI::RHIHandle under)
    {
        eastl::vector<uint32_t> positions;
        for (const VisibleInstance& visible : VisibleInstancesOf(draws, under, TranslucentBit))
        {
            positions.push_back(visible.m_position);
        }
        return positions;
    }
};

TEST_F(DrawListCullingSystemTest, AViewSeesWhatIsInItsFrustum)
{
    AddPass<TranslucentTag>("Translucency");
    AddInstance(0, Math::Vector3(0.0f, 0.0f, 10.0f));     // ahead
    AddInstance(1, Math::Vector3(50.0f, 0.0f, 10.0f));    // past the right side
    AddInstance(2, Math::Vector3(0.0f, 0.0f, 200.0f));    // past the far plane
    AddInstance(3, Math::Vector3(0.0f, 0.0f, -10.0f));    // behind the eye
    AddInstance(4, Math::Vector3(10.0f, 0.0f, 10.0f));    // across the right side
    system.Update(rhi, passes, draws);

    eastl::vector<uint32_t> positions = PositionsOf(view);
    eastl::sort(positions.begin(), positions.end());
    EXPECT_EQ(positions, (eastl::vector<uint32_t>{ 0, 4 }));
}

//! As InstanceCulling.hlsl, so a list draws the same set whichever culls it.
TEST_F(DrawListCullingSystemTest, TheNearPlaneCullsNothing)
{
    AddPass<TranslucentTag>("Translucency");
    AddInstance(0, Math::Vector3(0.0f, 0.0f, 0.5f), 0.1f);
    system.Update(rhi, passes, draws);

    EXPECT_EQ(PositionsOf(view), (eastl::vector<uint32_t>{ 0 }));
}

TEST_F(DrawListCullingSystemTest, AListDrawsBackToFront)
{
    AddPass<TranslucentTag>("Translucency");
    AddInstance(0, Math::Vector3(0.0f, 0.0f, 5.0f));
    AddInstance(1, Math::Vector3(0.0f, 0.0f, 20.0f));
    AddInstance(2, Math::Vector3(0.0f, 0.0f, 10.0f));
    system.Update(rhi, passes, draws);

    EXPECT_EQ(PositionsOf(view), (eastl::vector<uint32_t>{ 1, 2, 0 }));
    const VisibleInstanceRange visible = VisibleInstancesOf(draws, view, TranslucentBit);
    EXPECT_FLOAT_EQ(visible[0].m_depth, 20.0f);
}

TEST_F(DrawListCullingSystemTest, ThoseAsFarAreOrderedByKey)
{
    AddPass<TranslucentTag>("Translucency");
    const PipelineKey first  = Key(false).m_value < Key(true).m_value ? Key(false) : Key(true);
    const PipelineKey second = first == Key(false) ? Key(true) : Key(false);
    AddInstance(0, Math::Vector3(-1.0f, 0.0f, 10.0f), 0.5f, second);
    AddInstance(1, Math::Vector3(1.0f, 0.0f, 10.0f), 0.5f, first);
    system.Update(rhi, passes, draws);

    EXPECT_EQ(PositionsOf(view), (eastl::vector<uint32_t>{ 1, 0 }));
}

TEST_F(DrawListCullingSystemTest, AVisibleInstanceCarriesWhatItsRecordDraws)
{
    AddPass<TranslucentTag>("Translucency");
    AddInstance(3, Math::Vector3(0.0f, 0.0f, 10.0f), 0.5f, Key(true));
    system.Update(rhi, passes, draws);

    const VisibleInstanceRange visible = VisibleInstancesOf(draws, view, TranslucentBit);
    ASSERT_EQ(visible.size(), 1u);
    EXPECT_EQ(visible[0].m_view, view);
    EXPECT_EQ(visible[0].m_position, 3u);
    EXPECT_EQ(visible[0].m_key, Key(true));
    EXPECT_EQ(visible[0].m_indexCount, 36u);
    EXPECT_EQ(visible[0].m_firstIndex, 18u);
    EXPECT_EQ(visible[0].m_vertexOffset, 24);
}

//! Two passes drawing one list under one view cull it once.
TEST_F(DrawListCullingSystemTest, AListIsCulledOnceAView)
{
    AddPass<TranslucentTag>("Translucency");
    AddPass<TranslucentTag>("TranslucencyAgain");
    AddInstance(0, Math::Vector3(0.0f, 0.0f, 10.0f));
    system.Update(rhi, passes, draws);

    EXPECT_EQ(draws.GetView<VisibleInstance>().size(), 1u);
}

TEST_F(DrawListCullingSystemTest, EachViewHasItsOwnRun)
{
    const RHI::RHIHandle other   = AddView(true);
    const RHI::RHIHandle undrawn = AddView(false);
    AddPass<TranslucentTag>("Translucency");
    AddInstance(0, Math::Vector3(0.0f, 0.0f, 10.0f));
    AddInstance(1, Math::Vector3(0.0f, 0.0f, 20.0f));
    system.Update(rhi, passes, draws);

    EXPECT_EQ(PositionsOf(view), (eastl::vector<uint32_t>{ 1, 0 }));
    EXPECT_EQ(PositionsOf(other), (eastl::vector<uint32_t>{ 1, 0 }));
    EXPECT_TRUE(PositionsOf(undrawn).empty());
    for (const VisibleInstance& visible : VisibleInstancesOf(draws, other, TranslucentBit))
    {
        EXPECT_EQ(visible.m_view, other);
    }
}

TEST_F(DrawListCullingSystemTest, NoPassOfAListTheCpuCullsCullsNothing)
{
    AddPass<OpaqueTag>("Opaque");
    AddInstance(0, Math::Vector3(0.0f, 0.0f, 10.0f));
    system.Update(rhi, passes, draws);

    EXPECT_TRUE(PositionsOf(view).empty());
    EXPECT_EQ(draws.GetView<VisibleInstance>().size(), 0u);
}

//! A draw a visible instance, in the list's order: what the passes cut their groups out of.
TEST_F(DrawListCullingSystemTest, AListsDrawsAreWrittenInItsOrder)
{
    AddPass<TranslucentTag>("Translucency");
    AddInstance(0, Math::Vector3(0.0f, 0.0f, 5.0f));
    AddInstance(1, Math::Vector3(0.0f, 0.0f, 20.0f));
    AddInstance(2, Math::Vector3(0.0f, 0.0f, 10.0f));
    system.Update(rhi, passes, draws);

    const eastl::vector<RHI::DrawIndexedIndirectCommand> uploaded = UploadedDraws();
    ASSERT_EQ(uploaded.size(), 3u);
    const uint32_t order[] = { 1, 2, 0 };
    for (uint32_t i = 0; i < 3; ++i)
    {
        EXPECT_EQ(uploaded[i].m_firstInstance, order[i]);
        EXPECT_EQ(uploaded[i].m_instanceCount, 1u);
        EXPECT_EQ(uploaded[i].m_indexCount, 36u);
        EXPECT_EQ(uploaded[i].m_firstIndex, 6 * order[i]);
        EXPECT_EQ(uploaded[i].m_vertexOffset, 24);
    }
}

//! One buffer for every view: a view's draws start at its place in the sorted storage.
TEST_F(DrawListCullingSystemTest, AViewsDrawsStartAtItsPlace)
{
    const RHI::RHIHandle other = AddView(true);
    AddPass<TranslucentTag>("Translucency");
    AddInstance(0, Math::Vector3(0.0f, 0.0f, 10.0f));
    AddInstance(1, Math::Vector3(0.0f, 0.0f, 20.0f));
    system.Update(rhi, passes, draws);

    const VisibleInstanceRange first  = VisibleInstancesOf(draws, view, TranslucentBit);
    const VisibleInstanceRange second = VisibleInstancesOf(draws, other, TranslucentBit);
    const uint32_t firstStart  = FirstDrawOffset(draws, first);
    const uint32_t secondStart = FirstDrawOffset(draws, second);
    EXPECT_EQ(eastl::min(firstStart, secondStart), 0u);
    EXPECT_EQ(eastl::max(firstStart, secondStart), 2u);

    const eastl::vector<RHI::DrawIndexedIndirectCommand> uploaded = UploadedDraws();
    ASSERT_EQ(uploaded.size(), 4u);
    EXPECT_EQ(uploaded[secondStart].m_firstInstance, second[0].m_position);
}

TEST_F(DrawListCullingSystemTest, TheDrawsBufferIsAHostOneOfEveryFrame)
{
    AddPass<TranslucentTag>("Translucency");
    system.Update(rhi, passes, draws);

    const eastl::vector<RHI::RHIHandle> buffers = DrawArgumentsBuffers();
    ASSERT_EQ(buffers.size(), 1u);
    EXPECT_TRUE(rhi.Has<RHI::PerFrameTag>(buffers[0]));
    const RHI::PendingBufferInit& init = rhi.Get<RHI::PendingBufferInit>(buffers[0]);
    EXPECT_EQ(init.m_heapMemoryLevel, RHI::HeapMemoryLevel::Host);
    EXPECT_EQ(init.m_descriptor.m_bindFlags, RHI::BufferBindFlags::Indirect);
    EXPECT_EQ(init.m_descriptor.m_byteCount, 1024u * sizeof(RHI::DrawIndexedIndirectCommand));
    // Nothing seen, nothing to copy.
    EXPECT_FALSE(rhi.Has<RHI::PendingBufferMap>(buffers[0]));
}

TEST_F(DrawListCullingSystemTest, TheDrawsBufferGrowsAStepAtATime)
{
    AddPass<TranslucentTag>("Translucency");
    AddInstance(0, Math::Vector3(0.0f, 0.0f, 10.0f));
    system.Update(rhi, passes, draws);
    const RHI::RHIHandle small = DrawArgumentsBuffers()[0];

    draws.Clear();
    for (uint32_t position = 0; position < 1025; ++position)
    {
        AddInstance(position, Math::Vector3(0.0f, 0.0f, 10.0f));
    }
    system.Update(rhi, passes, draws);

    EXPECT_TRUE(rhi.Has<DeadTag>(small));
    RHI::RHIHandle grown = RHI::NullHandle;
    for (RHI::RHIHandle buffer : DrawArgumentsBuffers())
    {
        if (!rhi.Has<DeadTag>(buffer))
        {
            grown = buffer;
        }
    }
    ASSERT_NE(grown, RHI::NullHandle);
    EXPECT_EQ(rhi.Get<RHI::PendingBufferInit>(grown).m_descriptor.m_byteCount,
        2048u * sizeof(RHI::DrawIndexedIndirectCommand));
    EXPECT_EQ(rhi.Get<RHI::PendingBufferMap>(grown).m_byteCount, 1025u * sizeof(RHI::DrawIndexedIndirectCommand));
}

TEST_F(DrawListCullingSystemTest, ShutdownLeavesNothingBehind)
{
    AddPass<TranslucentTag>("Translucency");
    system.Update(rhi, passes, draws);
    const RHI::RHIHandle buffer = DrawArgumentsBuffers()[0];

    system.Shutdown(rhi);

    EXPECT_TRUE(rhi.Has<DeadTag>(buffer));
}