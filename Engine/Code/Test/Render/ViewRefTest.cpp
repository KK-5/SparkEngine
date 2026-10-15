#include <gtest/gtest.h>

#include <CoreComponents/Tags.h>
#include <ECS/ExecuteContext.h>
#include <ECS/WorldContext.h>

#include <RHI/Context/RHIContext.h>

#include <View/ShadowPools.h>
#include <View/ViewComponents.h>
#include <View/ViewFactory.h>
#include <View/ViewTags.h>

using namespace Spark;
using namespace Spark::Render;

//! A source destroyed outright, as opening a scene does, runs no sweep: the refs on it are
//! all that let its views go.
class ViewRefTest : public ::testing::Test
{
protected:
    WorldContext    world;
    RHI::RHIContext rhi;

    void SetUp() override
    {
        WorldExecuteContext::Push(world);
        RHI::RHIExecuteContext::Push(rhi);
    }

    void TearDown() override
    {
        RHI::RHIExecuteContext::Pop();
        WorldExecuteContext::Pop();
    }

    bool IsDead(RHI::RHIHandle h) const { return rhi.Has<DeadTag>(h); }
};

TEST_F(ViewRefTest, DestroyedCameraMarksItsViewsDead)
{
    const Entity camera = world.CreateEntity();
    world.Add<MainViewRef>(camera, MainViewRef{ CreateViewEntity<MainViewTag>(rhi) });
    world.Add<OutputViewRef>(camera, OutputViewRef{ CreateViewEntity<OutputViewTag>(rhi) });
    const RHI::RHIHandle main   = world.Get<MainViewRef>(camera).m_view.Get();
    const RHI::RHIHandle output = world.Get<OutputViewRef>(camera).m_view.Get();

    world.DestoryEntity(camera);

    EXPECT_TRUE(IsDead(main));
    EXPECT_TRUE(IsDead(output));
}

TEST_F(ViewRefTest, DestroyedLightMarksItsShadowViewsDeadAndGivesItsRowsBack)
{
    Ptr<ShadowRowPool> rows(new ShadowRowPool());

    ShadowViewRefs refs;
    for (uint32_t face = 0; face < kShadowCubeFaceCount; ++face)
    {
        refs.m_views.push_back(CreateViewEntity<ShadowViewTag>(rhi));
    }
    refs.m_rows = rows->Allocate(kShadowCubeFaceCount);
    const uint32_t base = refs.m_rows.Get();

    eastl::fixed_vector<RHI::RHIHandle, kShadowCubeFaceCount> views;
    for (const RHI::UniqueRHIHandle& view : refs.m_views)
    {
        views.push_back(view.Get());
    }

    const Entity light = world.CreateEntity();
    world.Add<ShadowViewRefs>(light, eastl::move(refs));

    world.DestoryEntity(light);

    for (RHI::RHIHandle view : views)
    {
        EXPECT_TRUE(IsDead(view));
    }
    EXPECT_EQ(rows->Allocate(kShadowCubeFaceCount).Get(), base);
}

TEST_F(ViewRefTest, RemovedRefMarksItsViewDead)
{
    const Entity camera = world.CreateEntity();
    world.Add<MainViewRef>(camera, MainViewRef{ CreateViewEntity<MainViewTag>(rhi) });
    const RHI::RHIHandle main = world.Get<MainViewRef>(camera).m_view.Get();

    world.Remove<MainViewRef>(camera);

    EXPECT_TRUE(IsDead(main));
}
