#include <gtest/gtest.h>

#include <EASTL/sort.h>
#include <EASTL/vector.h>

#include <Math/Frustum.h>
#include <Math/MathUtils.h>

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
    void AddInstance(uint32_t slot, const Math::Vector3& center, float half = 0.5f, PipelineKey key = Key(false))
    {
        DrawListInstance instance;
        instance.m_drawMaskBitIndex = TranslucentBit;
        instance.m_slot             = slot;
        instance.m_key              = key;
        instance.m_indexCount       = 36;
        instance.m_firstIndex       = 6 * slot;
        instance.m_vertexOffset     = 24;
        instance.m_worldBounds.min  = center - Math::Vector3(half);
        instance.m_worldBounds.max  = center + Math::Vector3(half);
        draws.Add<DrawListInstance>(draws.CreateEntity(), instance);
    }

    //! The slots under `under`, in the order they draw.
    eastl::vector<uint32_t> SlotsOf(RHI::RHIHandle under)
    {
        eastl::vector<uint32_t> slots;
        for (const VisibleInstance& visible : VisibleInstancesOf(draws, under, TranslucentBit))
        {
            slots.push_back(visible.m_slot);
        }
        return slots;
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

    eastl::vector<uint32_t> slots = SlotsOf(view);
    eastl::sort(slots.begin(), slots.end());
    EXPECT_EQ(slots, (eastl::vector<uint32_t>{ 0, 4 }));
}

//! As InstanceCulling.hlsl, so a list draws the same set whichever culls it.
TEST_F(DrawListCullingSystemTest, TheNearPlaneCullsNothing)
{
    AddPass<TranslucentTag>("Translucency");
    AddInstance(0, Math::Vector3(0.0f, 0.0f, 0.5f), 0.1f);
    system.Update(rhi, passes, draws);

    EXPECT_EQ(SlotsOf(view), (eastl::vector<uint32_t>{ 0 }));
}

TEST_F(DrawListCullingSystemTest, AListDrawsBackToFront)
{
    AddPass<TranslucentTag>("Translucency");
    AddInstance(0, Math::Vector3(0.0f, 0.0f, 5.0f));
    AddInstance(1, Math::Vector3(0.0f, 0.0f, 20.0f));
    AddInstance(2, Math::Vector3(0.0f, 0.0f, 10.0f));
    system.Update(rhi, passes, draws);

    EXPECT_EQ(SlotsOf(view), (eastl::vector<uint32_t>{ 1, 2, 0 }));
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

    EXPECT_EQ(SlotsOf(view), (eastl::vector<uint32_t>{ 1, 0 }));
}

TEST_F(DrawListCullingSystemTest, AVisibleInstanceCarriesWhatItsRecordDraws)
{
    AddPass<TranslucentTag>("Translucency");
    AddInstance(3, Math::Vector3(0.0f, 0.0f, 10.0f), 0.5f, Key(true));
    system.Update(rhi, passes, draws);

    const VisibleInstanceRange visible = VisibleInstancesOf(draws, view, TranslucentBit);
    ASSERT_EQ(visible.size(), 1u);
    EXPECT_EQ(visible[0].m_view, view);
    EXPECT_EQ(visible[0].m_slot, 3u);
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

    EXPECT_EQ(SlotsOf(view), (eastl::vector<uint32_t>{ 1, 0 }));
    EXPECT_EQ(SlotsOf(other), (eastl::vector<uint32_t>{ 1, 0 }));
    EXPECT_TRUE(SlotsOf(undrawn).empty());
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

    EXPECT_TRUE(SlotsOf(view).empty());
    EXPECT_EQ(draws.GetView<VisibleInstance>().size(), 0u);
}
