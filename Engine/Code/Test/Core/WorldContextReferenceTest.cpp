#include <gtest/gtest.h>

#include <ECS/Common.h>
#include <ECS/ExecuteContext.h>
#include <ECS/SystemTraits.h>
#include <ECS/WorldContext.h>

using namespace Spark;

namespace
{
    struct RefPosition
    {
        float x;
        float y;
    };

    struct RefVelocity
    {
        float dx;
        float dy;
    };

    struct ReadPositionSystem
    {
        using ComponentAcquires = Spark::ComponentAcquires<
            Spark::ReadComponent<RefPosition>
        >;
    };

    struct WritePositionSystem
    {
        using ComponentAcquires = Spark::ComponentAcquires<
            Spark::WriteComponent<RefPosition>
        >;
    };

    struct ReadAllSystem
    {
        using ComponentAcquires = Spark::ComponentAcquires<
            Spark::ReadComponent<Spark::All>
        >;
    };

    using ReadPositionTraits = Spark::SystemTraits<ReadPositionSystem>;
    using WritePositionTraits = Spark::SystemTraits<WritePositionSystem>;
    using ReadAllTraits = Spark::SystemTraits<ReadAllSystem>;

    using ReadPositionRef = Spark::WorldContextReference<ReadPositionTraits>;
    using WritePositionRef = Spark::WorldContextReference<WritePositionTraits>;
    using ReadAllRef = Spark::WorldContextReference<ReadAllTraits>;

    static_assert(ReadPositionRef::CanReadComponentV<RefPosition>, "read ref should read RefPosition");
    static_assert(!ReadPositionRef::CanWriteComponentV<RefPosition>, "read ref should not write RefPosition");
    static_assert(!ReadPositionRef::CanReadComponentV<RefVelocity>, "read ref should not read RefVelocity");

    static_assert(WritePositionRef::CanWriteComponentV<RefPosition>, "write ref should write RefPosition");
    static_assert(!WritePositionRef::CanReadComponentV<RefPosition>, "write ref should not read RefPosition");

    static_assert(ReadAllRef::CanReadComponentV<RefPosition>, "read-all should read RefPosition");
    static_assert(ReadAllRef::CanReadComponentV<RefVelocity>, "read-all should read RefVelocity");
    static_assert(!ReadAllRef::CanWriteComponentV<RefPosition>, "read-all should not write RefPosition");
}

TEST(WorldContextReferenceTest, CurrentReferenceBindsToCurrentWorldContext)
{
    WorldContext context;

    {
        WorldExecuteContextGuard guard(context);
        auto ref = WorldExecuteContext::CurrentReference<WritePositionTraits>();
        ASSERT_EQ(&ref.GetContext(), &context);
    }
}

TEST(WorldContextReferenceTest, WriteReferenceCanMutateAuthorizedComponent)
{
    WorldContext context;
    const Entity entity = context.CreateEntity();

    {
        WorldExecuteContextGuard guard(context);
        auto writer = WorldExecuteContext::CurrentReference<WritePositionTraits>();

        writer.Add<RefPosition>(entity, 1.0f, 2.0f);
        auto& pos = writer.Get<RefPosition>(entity);
        pos.x += 3.0f;
        pos.y += 5.0f;
    }

    const auto pos = context.Get<RefPosition>(entity);
    EXPECT_FLOAT_EQ(pos.x, 4.0f);
    EXPECT_FLOAT_EQ(pos.y, 7.0f);
}

TEST(WorldContextReferenceTest, ReadReferenceCanReadAuthorizedComponent)
{
    WorldContext context;
    const Entity entity = context.CreateEntity();
    context.Add<RefPosition>(entity, 10.0f, 20.0f);

    {
        WorldExecuteContextGuard guard(context);
        const auto reader = WorldExecuteContext::CurrentReference<ReadPositionTraits>();
        const auto pos = reader.Get<RefPosition>(entity);
        EXPECT_FLOAT_EQ(pos.x, 10.0f);
        EXPECT_FLOAT_EQ(pos.y, 20.0f);
    }
}

TEST(WorldContextReferenceTest, ReadAllReferenceCanReadDifferentComponents)
{
    WorldContext context;
    const Entity entity = context.CreateEntity();
    context.Add<RefPosition>(entity, 2.0f, 4.0f);
    context.Add<RefVelocity>(entity, 0.5f, 0.25f);

    {
        WorldExecuteContextGuard guard(context);
        const auto reader = WorldExecuteContext::CurrentReference<ReadAllTraits>();
        const auto pos = reader.Get<RefPosition>(entity);
        const auto vel = reader.Get<RefVelocity>(entity);

        EXPECT_FLOAT_EQ(pos.x, 2.0f);
        EXPECT_FLOAT_EQ(pos.y, 4.0f);
        EXPECT_FLOAT_EQ(vel.dx, 0.5f);
        EXPECT_FLOAT_EQ(vel.dy, 0.25f);
    }
}
