#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <ECS/Entity.h>
#include <ECS/WorldContext.h>
#include <ECS/StagingContext.h>

using namespace Spark;

namespace
{
    struct MergeTestPosition
    {
        float x;
        float y;
    };

    /// Destroy and recreate the same slot until its version has moved on.
    Entity BumpVersion(WorldContext& context, Entity entity, uint32_t times)
    {
        Entity current = entity;
        for (uint32_t i = 0; i < times; ++i)
        {
            context.DestoryEntity(current);
            current = context.CreateEntity();
        }
        return current;
    }
}

TEST(MergeTest, EntityAtEmptySlot)
{
    WorldContext context;

    Entity entity = context.CreateEntity();
    context.DestoryEntity(entity);

    // The slot exists but holds nobody.
    ASSERT_EQ(context.EntityAt(entity), NullEntity);
}

TEST(MergeTest, EntityAtNeverAllocatedSlot)
{
    WorldContext context;

    const Entity far = entt::entt_traits<Entity>::construct(4096, 0);
    ASSERT_EQ(context.EntityAt(far), NullEntity);
}

TEST(MergeTest, EntityAtSameVersion)
{
    WorldContext context;

    Entity entity = context.CreateEntity();
    ASSERT_EQ(context.EntityAt(entity), entity);
}

TEST(MergeTest, EntityAtDifferentVersion)
{
    WorldContext context;

    Entity entity = context.CreateEntity();
    Entity recycled = BumpVersion(context, entity, 1);

    // Same slot, moved-on version: the stale value is no longer a valid identity...
    ASSERT_EQ(entt::to_entity(recycled), entt::to_entity(entity));
    ASSERT_NE(recycled, entity);
    ASSERT_FALSE(context.Valid(entity));

    // ...but the slot is occupied, and EntityAt is what answers that.
    ASSERT_EQ(context.EntityAt(entity), recycled);
}

TEST(MergeTest, CreateEntityHintOnFreeSlotRestoresIt)
{
    WorldContext context;
    StagingContext<Entity> staging;

    Entity source = staging.CreateEntity();

    // Nothing occupies that slot in the target yet.
    ASSERT_EQ(context.CreateEntity(source), source);
}

TEST(MergeTest, CreateEntityHintOnTakenSlotRenumbers)
{
    WorldContext context;
    StagingContext<Entity> staging;

    Entity occupant = context.CreateEntity();
    Entity source = staging.CreateEntity();
    ASSERT_EQ(entt::to_entity(source), entt::to_entity(occupant));

    Entity created = context.CreateEntity(source);

    ASSERT_NE(created, source);
    ASSERT_TRUE(context.Valid(created));
    ASSERT_EQ(context.EntityAt(source), occupant);
}

TEST(MergeTest, CreateEntityHintCollidesAcrossVersions)
{
    WorldContext context;
    StagingContext<Entity> staging;

    // Occupant sits on slot 0 with a version the staging entity cannot have.
    Entity occupant = BumpVersion(context, context.CreateEntity(), 3);
    Entity source = staging.CreateEntity();
    ASSERT_EQ(entt::to_entity(source), entt::to_entity(occupant));
    ASSERT_NE(source, occupant);

    // Valid() says no, yet the hint still collides — occupancy is decided by slot alone.
    ASSERT_FALSE(context.Valid(source));
    Entity created = context.CreateEntity(source);

    ASSERT_NE(created, source);
    ASSERT_EQ(context.EntityAt(source), occupant);
}

TEST(MergeTest, StagingContextHoldsComponents)
{
    StagingContext<Entity> staging;

    Entity entity = staging.CreateEntity();
    staging.Add<MergeTestPosition>(entity, MergeTestPosition{1.0f, 2.0f});

    ASSERT_TRUE(staging.Has<MergeTestPosition>(entity));
    ASSERT_EQ(staging.Get<MergeTestPosition>(entity).x, 1.0f);

    auto view = staging.GetView<MergeTestPosition>();
    ASSERT_EQ(view.size(), 1u);
}

TEST(MergeTest, StagingContextGetStorageConstDoesNotCreate)
{
    StagingContext<Entity> staging;

    const auto& constStaging = staging;
    ASSERT_EQ(constStaging.GetStorage<MergeTestPosition>(), nullptr);

    ASSERT_EQ(staging.GetStorage<MergeTestPosition>().size(), 0u);
    ASSERT_NE(constStaging.GetStorage<MergeTestPosition>(), nullptr);
}

TEST(MergeTest, StagingContextIsMovable)
{
    StagingContext<Entity> staging;

    Entity entity = staging.CreateEntity();
    staging.Add<MergeTestPosition>(entity, MergeTestPosition{3.0f, 4.0f});

    StagingContext<Entity> moved = eastl::move(staging);

    ASSERT_TRUE(moved.Valid(entity));
    ASSERT_EQ(moved.Get<MergeTestPosition>(entity).y, 4.0f);
}
