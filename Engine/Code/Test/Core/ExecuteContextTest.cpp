#include <gtest/gtest.h>

#include <ECS/ExecuteContext.h>
#include <ECS/WorldContext.h>

using namespace Spark;

TEST(ExecuteContextTest, InitialCurrentIsNull)
{
    ASSERT_EQ(WorldExecuteContext::Current(), nullptr);
}

TEST(ExecuteContextTest, PushAndPop)
{
    WorldContext ctx;

    WorldExecuteContext::Push(ctx);
    ASSERT_EQ(WorldExecuteContext::Current(), &ctx);

    WorldExecuteContext::Pop();
    ASSERT_EQ(WorldExecuteContext::Current(), nullptr);
}

TEST(ExecuteContextTest, NestedPushPop)
{
    WorldContext ctx1;
    WorldContext ctx2;
    WorldContext ctx3;

    WorldExecuteContext::Push(ctx1);
    ASSERT_EQ(WorldExecuteContext::Current(), &ctx1);

    WorldExecuteContext::Push(ctx2);
    ASSERT_EQ(WorldExecuteContext::Current(), &ctx2);

    WorldExecuteContext::Push(ctx3);
    ASSERT_EQ(WorldExecuteContext::Current(), &ctx3);

    WorldExecuteContext::Pop();
    ASSERT_EQ(WorldExecuteContext::Current(), &ctx2);

    WorldExecuteContext::Pop();
    ASSERT_EQ(WorldExecuteContext::Current(), &ctx1);

    WorldExecuteContext::Pop();
    ASSERT_EQ(WorldExecuteContext::Current(), nullptr);
}

TEST(ExecuteContextTest, GuardBasic)
{
    WorldContext ctx;

    {
        WorldExecuteContextGuard guard(ctx);
        ASSERT_EQ(WorldExecuteContext::Current(), &ctx);
    }

    ASSERT_EQ(WorldExecuteContext::Current(), nullptr);
}

TEST(ExecuteContextTest, GuardNested)
{
    WorldContext ctx1;
    WorldContext ctx2;

    {
        WorldExecuteContextGuard guard1(ctx1);
        ASSERT_EQ(WorldExecuteContext::Current(), &ctx1);

        {
            WorldExecuteContextGuard guard2(ctx2);
            ASSERT_EQ(WorldExecuteContext::Current(), &ctx2);
        }

        ASSERT_EQ(WorldExecuteContext::Current(), &ctx1);
    }

    ASSERT_EQ(WorldExecuteContext::Current(), nullptr);
}

TEST(ExecuteContextTest, MixPushPopAndGuard)
{
    WorldContext ctx1;
    WorldContext ctx2;

    WorldExecuteContext::Push(ctx1);
    ASSERT_EQ(WorldExecuteContext::Current(), &ctx1);

    {
        WorldExecuteContextGuard guard(ctx2);
        ASSERT_EQ(WorldExecuteContext::Current(), &ctx2);
    }

    ASSERT_EQ(WorldExecuteContext::Current(), &ctx1);

    WorldExecuteContext::Pop();
    ASSERT_EQ(WorldExecuteContext::Current(), nullptr);
}
