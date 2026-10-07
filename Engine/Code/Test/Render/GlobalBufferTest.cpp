#include <gtest/gtest.h>

#include <CoreComponents/Tags.h>
#include <ECS/WorldContext.h>

#include <RHI/Context/RHIContext.h>

#include <Binding/GlobalBuffer.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    struct TestRows {};

    struct Amount { uint32_t m_value = 0; };
    struct Factor { uint32_t m_value = 1; };

    //! m_sum is only ever added to, so a record that was not reset shows what its previous
    //! holder left.
    struct Row
    {
        uint32_t m_sum = 0;
    };

    using TestBuffer = GlobalBuffer<TestRows, Row, Amount, Factor>;
    using TestSlot   = TestBuffer::Slot;
}

//! Who holds a slot and what the mirror says about a slot nobody holds. UpdateMirror touches
//! the two contexts and the mirror only, so no device is needed; binding and uploading the
//! buffer are Update's and are not run here.
class GlobalBufferTest : public ::testing::Test
{
protected:
    WorldContext    world;
    RHI::RHIContext rhi;
    TestBuffer      buffer;

    void SetUp() override
    {
        TestBuffer::Descriptor descriptor;
        descriptor.m_capacity     = 4;
        descriptor.m_resourceName = ObjectName("g_TestRows");
        buffer.Init(rhi, descriptor);
    }

    void TearDown() override
    {
        buffer.Shutdown(world, rhi);
    }

    Entity MakeSource(uint32_t amount)
    {
        const Entity e = world.CreateEntity();
        world.Add<Amount>(e, Amount{ amount });
        world.Add<Factor>(e);
        return e;
    }

    void Update()
    {
        buffer.UpdateMirror(world, [](Entity, Row& row, const Amount& amount, const Factor& factor)
        {
            row.m_sum += amount.m_value * factor.m_value;
        });
    }

    uint32_t SlotOf(Entity e) const { return world.Get<TestSlot>(e).Get(); }
};

TEST_F(GlobalBufferTest, AnEntityWithEverySourceGetsASlotAndIsEncoded)
{
    const Entity e = MakeSource(5);

    const Entity partial = world.CreateEntity();
    world.Add<Amount>(partial, Amount{ 9 });

    Update();

    ASSERT_TRUE(world.Has<TestSlot>(e));
    EXPECT_FALSE(world.Has<TestSlot>(partial));
    EXPECT_EQ(buffer.Size(), 1u);
    EXPECT_EQ(buffer[SlotOf(e)].m_sum, 5u);
}

TEST_F(GlobalBufferTest, ASlotGivenBackReadsAsDefault)
{
    const Entity e = MakeSource(5);
    Update();
    const uint32_t slot = SlotOf(e);

    world.DestoryEntity(e);
    Update();

    EXPECT_EQ(buffer[slot].m_sum, 0u);
    // The hole stays inside the uploaded range.
    EXPECT_EQ(buffer.Size(), 1u);
}

TEST_F(GlobalBufferTest, ANewHolderStartsFromDefault)
{
    const Entity first = MakeSource(5);
    Update();
    const uint32_t slot = SlotOf(first);

    // Given back and taken again within one update.
    world.DestoryEntity(first);
    const Entity second = MakeSource(3);
    Update();

    ASSERT_EQ(SlotOf(second), slot);
    EXPECT_EQ(buffer[slot].m_sum, 3u);
}

//! Its last frame: still drawn from the record it had, like everything else that reads the slot.
TEST_F(GlobalBufferTest, ADeadEntityKeepsItsSlotAndRecordUntilDestroyed)
{
    const Entity dead = MakeSource(5);
    Update();
    const uint32_t slot = SlotOf(dead);

    world.Add<DeadTag>(dead);
    const Entity other = MakeSource(3);
    Update();

    EXPECT_EQ(buffer[slot].m_sum, 5u);
    ASSERT_TRUE(world.Has<TestSlot>(dead));
    EXPECT_NE(SlotOf(other), slot);

    world.DestoryEntity(dead);
    Update();

    EXPECT_EQ(buffer[slot].m_sum, 0u);
}

TEST_F(GlobalBufferTest, AnEntityThatLostASourceGivesItsSlotBack)
{
    const Entity e = MakeSource(5);
    Update();
    const uint32_t slot = SlotOf(e);

    world.Remove<Factor>(e);
    Update();

    EXPECT_FALSE(world.Has<TestSlot>(e));
    EXPECT_EQ(buffer[slot].m_sum, 0u);

    world.Add<Factor>(e);
    Update();

    ASSERT_TRUE(world.Has<TestSlot>(e));
    EXPECT_EQ(buffer[SlotOf(e)].m_sum, 5u);
}
