#include <gtest/gtest.h>

#include <ECS/WorldContext.h>

#include <Binding/SlotPool.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    struct TestSlots {};

    using TestPool = SlotPool<TestSlots>;
    using TestRef  = SlotRef<TestSlots>;

    Ptr<TestPool> MakePool(uint32_t capacity)
    {
        Ptr<TestPool> pool(new TestPool());
        pool->Init(capacity);
        return pool;
    }
}

TEST(SlotPoolTest, TheLowestFreeSlotGoesOutFirst)
{
    Ptr<TestPool> pool = MakePool(4);

    TestRef a = pool->Allocate();
    TestRef b = pool->Allocate();
    TestRef c = pool->Allocate();
    EXPECT_EQ(a.Get(), 0u);
    EXPECT_EQ(b.Get(), 1u);
    EXPECT_EQ(c.Get(), 2u);

    b.Reset();
    TestRef reused = pool->Allocate();
    EXPECT_EQ(reused.Get(), 1u);
}

TEST(SlotPoolTest, TheWaterMarkIsTheUploadLength)
{
    Ptr<TestPool> pool = MakePool(4);

    TestRef a = pool->Allocate();
    TestRef b = pool->Allocate();
    EXPECT_EQ(pool->Bound(), 2u);

    // Reuse does not raise it, and the freed slot stays inside the uploaded range.
    a.Reset();
    EXPECT_EQ(pool->Bound(), 2u);
    TestRef reused = pool->Allocate();
    EXPECT_EQ(reused.Get(), 0u);
    EXPECT_EQ(pool->Bound(), 2u);
}

TEST(SlotPoolTest, AFullPoolHandsOutAnEmptyRef)
{
    Ptr<TestPool> pool = MakePool(1);

    TestRef first = pool->Allocate();
    EXPECT_TRUE(first.IsValid());
    EXPECT_FALSE(pool->Allocate().IsValid());
}

//! The path that leaked before: destroying the entity outright, without ever marking it,
//! is what ClearScene and a direct DestoryEntity both do.
TEST(SlotPoolTest, ASlotComesBackWhenItsEntityIsDestroyedOutright)
{
    Ptr<TestPool> pool = MakePool(4);

    WorldContext world;
    const Entity entity = world.CreateEntity();
    world.Add<TestRef>(entity, pool->Allocate());
    ASSERT_EQ(pool->Bound(), 1u);

    world.DestoryEntity(entity);

    TestRef reused = pool->Allocate();
    EXPECT_EQ(reused.Get(), 0u);
    EXPECT_EQ(pool->Bound(), 1u);
}

TEST(SlotPoolTest, ClearingTheContextReturnsEverySlot)
{
    Ptr<TestPool> pool = MakePool(4);

    {
        WorldContext world;
        for (uint32_t i = 0; i < 3; ++i)
        {
            world.Add<TestRef>(world.CreateEntity(), pool->Allocate());
        }
        ASSERT_EQ(pool->Bound(), 3u);
    }

    TestRef first  = pool->Allocate();
    TestRef second = pool->Allocate();
    EXPECT_EQ(first.Get(), 0u);
    EXPECT_EQ(second.Get(), 1u);
    EXPECT_EQ(pool->Bound(), 3u);
}

TEST(SlotPoolTest, AnObserverSeesTheSlotGoWhenTheEntityDoes)
{
    Ptr<TestPool> pool = MakePool(4);

    WorldContext world;
    const Entity entity = world.CreateEntity();
    const SlotWeakRef<TestSlots> observer =
        world.Add<TestRef>(entity, pool->Allocate()).Weak();
    ASSERT_TRUE(observer.IsValid());

    world.DestoryEntity(entity);

    EXPECT_FALSE(observer.IsValid());
}
