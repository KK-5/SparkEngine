#include <gtest/gtest.h>

#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include <Binding/Instance/InstanceBinding.h>
#include <Binding/View/ViewBinding.h>
#include <Feature/InstanceLists/InstanceLists.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    eastl::vector<RHI::AttachmentId> AllNames()
    {
        return {
            InstanceLists::Opaque::VisibleInstancesName(),
            InstanceLists::Opaque::VisibleCountName(),
            InstanceLists::ShadowCaster::VisibleInstancesName(),
            InstanceLists::ShadowCaster::VisibleCountName(),
            InstanceLists::DepthPre::IndirectLayoutName(),
            InstanceLists::DepthPre::DrawArgumentsName(),
            InstanceLists::DepthPre::DrawCountName(),
            InstanceLists::GBuffer::IndirectLayoutName(),
            InstanceLists::GBuffer::DrawArgumentsName(),
            InstanceLists::GBuffer::DrawCountName(),
            InstanceLists::Shadow::IndirectLayoutName(),
            InstanceLists::Shadow::DrawArgumentsName(),
            InstanceLists::Shadow::DrawCountName(),
        };
    }
}

//! Two buffers under one name would be one buffer written by two passes.
TEST(InstanceListsTest, EachBufferHasANameOfItsOwn)
{
    const auto names = AllNames();
    for (size_t i = 0; i < names.size(); ++i)
    {
        for (size_t j = i + 1; j < names.size(); ++j)
        {
            EXPECT_NE(names[i], names[j]) << names[i].GetCStr();
        }
    }
}

//! The culling pass and every binning pass reading its list lay it out by this, so it is
//! what the two agree on.
class CullingLayoutTest : public ::testing::Test
{
protected:
    RHI::RHIContext rhi;
    Ptr<SlotPool<Views>> viewSlots{ new SlotPool<Views>() };

    void SetUp() override
    {
        viewSlots->Init(8);
    }

    RHI::RHIHandle ViewWithRow()
    {
        const RHI::RHIHandle view = rhi.CreateEntity();
        rhi.Add<ViewSlotRef>(view, viewSlots->Allocate());
        return view;
    }

    void SetSlotCount(uint32_t count)
    {
        const RHI::RHIHandle entity = rhi.CreateEntity();
        rhi.Add<InstanceSlotCount>(entity, InstanceSlotCount{ count });
    }
};

TEST_F(CullingLayoutTest, AViewWithoutARowIsLeftOutAndTheOthersKeepTheirOrder)
{
    const RHI::RHIHandle first  = ViewWithRow();
    const RHI::RHIHandle noRow  = rhi.CreateEntity();
    const RHI::RHIHandle second = ViewWithRow();

    ViewHandleList views;
    views.push_back(first);
    views.push_back(noRow);
    views.push_back(second);
    const InstanceLists::CullingLayout layout = InstanceLists::LayOutCulling(rhi, views);

    ASSERT_EQ(layout.m_views.size(), 2u);
    EXPECT_EQ(layout.m_views[0].m_view, first);
    EXPECT_EQ(layout.m_views[0].m_viewIndex, rhi.Get<ViewSlotRef>(first).Get());
    EXPECT_EQ(layout.m_views[1].m_view, second);
    EXPECT_EQ(layout.m_views[1].m_viewIndex, rhi.Get<ViewSlotRef>(second).Get());
}

TEST_F(CullingLayoutTest, EveryViewHasRoomForEverySlotRoundedUp)
{
    SetSlotCount(1500);
    ViewHandleList views;
    for (int i = 0; i < 5; ++i)
    {
        views.push_back(ViewWithRow());
    }
    const InstanceLists::CullingLayout layout = InstanceLists::LayOutCulling(rhi, views);

    EXPECT_EQ(layout.m_slotCount, 1500u);
    EXPECT_EQ(layout.m_maxPerView, 2048u);
    EXPECT_EQ(layout.m_viewCapacity, 8u);
}

//! The buffers exist in every frame, so there is room for something when there is nothing.
TEST_F(CullingLayoutTest, NoViewAndNoSlotStillMakeRoom)
{
    const InstanceLists::CullingLayout layout = InstanceLists::LayOutCulling(rhi, ViewHandleList{});

    EXPECT_TRUE(layout.m_views.empty());
    EXPECT_EQ(layout.m_slotCount, 0u);
    EXPECT_GT(layout.m_maxPerView, 0u);
    EXPECT_GT(layout.m_viewCapacity, 0u);
}
