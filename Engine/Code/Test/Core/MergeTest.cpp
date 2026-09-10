#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <ECS/Common.h>
#include <ECS/Entity.h>
#include <ECS/WorldContext.h>
#include <ECS/ContextStorage.h>
#include <ECS/StagingContext.h>
#include <ECS/Merge/ContextMerge.h>
#include <Service/Service.h>
#include <SceneManager/Component/HierarchyComponent.h>
#include <SceneManager/IScene.h>
#include <SceneManager/SceneManager.h>

#include <EASTL/array.h>

using namespace Spark;

namespace
{
    struct MergeTestPosition
    {
        float x;
        float y;
    };

    struct MergeTestVelocity
    {
        float dx;
        float dy;
    };

    struct MergeTestTag
    {
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

namespace Spark
{
    SPARK_COMPONENT_TRAITS(MergeTestVelocity,
        static constexpr ComponentEventMask componentEvents = ComponentEventMask::Create;
    )
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

TEST(MergeTest, MergeIntoEmptyTargetKeepsIdentifiers)
{
    WorldContext world;
    StagingContext<Entity> staging;

    Entity a = staging.CreateEntity();
    Entity b = staging.CreateEntity();
    staging.Add<MergeTestPosition>(a, MergeTestPosition{1.0f, 2.0f});
    staging.Add<MergeTestPosition>(b, MergeTestPosition{3.0f, 4.0f});

    auto result = Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, eastl::move(staging));

    // The target was empty, so every hint was honoured. created is in storage order, which is not
    // insertion order -- the merge promises no ordering.
    ASSERT_EQ(result.created.size(), 2u);
    ASSERT_TRUE(world.Valid(a));
    ASSERT_TRUE(world.Valid(b));
    ASSERT_EQ(world.Get<MergeTestPosition>(a).x, 1.0f);
    ASSERT_EQ(world.Get<MergeTestPosition>(b).x, 3.0f);
}

TEST(MergeTest, MergeRenumbersOnCollision)
{
    WorldContext world;
    StagingContext<Entity> staging;

    Entity occupant = world.CreateEntity();
    Entity source = staging.CreateEntity();
    ASSERT_EQ(entt::to_entity(source), entt::to_entity(occupant));
    staging.Add<MergeTestPosition>(source, MergeTestPosition{5.0f, 6.0f});

    auto result = Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, eastl::move(staging));

    ASSERT_EQ(result.created.size(), 1u);
    ASSERT_NE(result.created[0], source);
    ASSERT_EQ(world.Get<MergeTestPosition>(result.created[0]).x, 5.0f);

    // The occupant kept its identifier and gained nothing.
    ASSERT_TRUE(world.Valid(occupant));
    ASSERT_FALSE(world.Has<MergeTestPosition>(occupant));
}

TEST(MergeTest, MergeCollidesAcrossVersions)
{
    WorldContext world;
    StagingContext<Entity> staging;

    Entity occupant = BumpVersion(world, world.CreateEntity(), 3);
    Entity source = staging.CreateEntity();
    ASSERT_NE(source, occupant);
    ASSERT_FALSE(world.Valid(source));
    staging.Add<MergeTestPosition>(source, MergeTestPosition{7.0f, 8.0f});

    auto result = Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, eastl::move(staging));

    // Identity said the slot was free, occupancy said otherwise -- the component must not land on
    // the occupant.
    ASSERT_EQ(result.created.size(), 1u);
    ASSERT_NE(result.created[0], source);
    ASSERT_FALSE(world.Has<MergeTestPosition>(occupant));
    ASSERT_EQ(world.Get<MergeTestPosition>(result.created[0]).x, 7.0f);
}

TEST(MergeTest, MergeVisitsAnEntityOnceAcrossTypes)
{
    WorldContext world;
    StagingContext<Entity> staging;

    Entity both = staging.CreateEntity();
    staging.Add<MergeTestPosition>(both, MergeTestPosition{1.0f, 1.0f});
    staging.Add<MergeTestVelocity>(both, MergeTestVelocity{2.0f, 2.0f});

    auto result = Merge<MergeMatch::Any, MergeMapping::Remap,
                        MergeTestPosition, MergeTestVelocity>(world, eastl::move(staging));

    ASSERT_EQ(result.created.size(), 1u);
    ASSERT_TRUE(world.Has<MergeTestPosition>(result.created[0]));
    ASSERT_TRUE(world.Has<MergeTestVelocity>(result.created[0]));
}

TEST(MergeTest, MergeAllSkipsPartialEntities)
{
    WorldContext world;
    StagingContext<Entity> staging;

    Entity both = staging.CreateEntity();
    Entity partial = staging.CreateEntity();
    staging.Add<MergeTestPosition>(both, MergeTestPosition{1.0f, 1.0f});
    staging.Add<MergeTestVelocity>(both, MergeTestVelocity{2.0f, 2.0f});
    staging.Add<MergeTestPosition>(partial, MergeTestPosition{9.0f, 9.0f});

    auto result = Merge<MergeMatch::All, MergeMapping::Remap,
                        MergeTestPosition, MergeTestVelocity>(world, eastl::move(staging));

    ASSERT_EQ(result.created.size(), 1u);
    ASSERT_EQ(world.Get<MergeTestPosition>(result.created[0]).x, 1.0f);
    ASSERT_EQ(world.GetView<MergeTestPosition>().size(), 1u);
}

TEST(MergeTest, MergeCarriesTagComponents)
{
    WorldContext world;
    StagingContext<Entity> staging;

    Entity entity = staging.CreateEntity();
    staging.Add<MergeTestTag>(entity);

    auto result = Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestTag>(world, eastl::move(staging));

    ASSERT_EQ(result.created.size(), 1u);
    ASSERT_TRUE(world.Has<MergeTestTag>(result.created[0]));
}

TEST(MergeTest, MergeIgnoresTypesAbsentFromSource)
{
    WorldContext world;
    StagingContext<Entity> staging;

    Entity entity = staging.CreateEntity();
    staging.Add<MergeTestPosition>(entity, MergeTestPosition{1.0f, 2.0f});

    auto result = Merge<MergeMatch::Any, MergeMapping::Remap,
                        MergeTestPosition, MergeTestVelocity>(world, eastl::move(staging));

    ASSERT_EQ(result.created.size(), 1u);
    ASSERT_FALSE(world.Has<MergeTestVelocity>(result.created[0]));
}

TEST(MergeTest, MergeClearsItsBookkeeping)
{
    WorldContext world;
    StagingContext<Entity> staging;

    world.CreateEntity();   // forces a collision, so MergedTo gets written
    Entity source = staging.CreateEntity();
    staging.Add<MergeTestPosition>(source, MergeTestPosition{1.0f, 2.0f});

    Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, eastl::move(staging));

    ASSERT_EQ(world.GetView<MergedFrom<Entity>>().size(), 0u);
    ASSERT_EQ(world.GetView<MergedTo<Entity>>().size(), 0u);
}

namespace
{
    /// Counts what reaches a handler, and whether the whole batch was already in place when it did.
    class MergeProbe : public ComponentEventBus::Handler
    {
    public:
        explicit MergeProbe(TypeId typeId)
        {
            ComponentEventBus::Handler::BusConnect(typeId);
        }

        ~MergeProbe() override
        {
            ComponentEventBus::Handler::BusDisconnect();
        }

        void OnComponentConstruct(Entity) override
        {
            ++singleCalls;
        }

        void OnComponentsConstruct(eastl::span<const Entity> entities) override
        {
            ++batchCalls;
            for (Entity entity : entities)
            {
                seen.push_back(entity);
            }
        }

        uint32_t singleCalls = 0;
        uint32_t batchCalls = 0;
        eastl::vector<Entity> seen;
    };
}

TEST(MergeTest, MergeAnnouncesOneBatchPerType)
{
    WorldContext world;
    WorldExecuteContext::Push(world);
    MergeProbe probe(GetTypeId<MergeTestVelocity>());

    StagingContext<Entity> staging;
    Entity withBoth = staging.CreateEntity();
    Entity positionOnly = staging.CreateEntity();
    staging.Add<MergeTestPosition>(withBoth, MergeTestPosition{1.0f, 1.0f});
    staging.Add<MergeTestVelocity>(withBoth, MergeTestVelocity{2.0f, 2.0f});
    staging.Add<MergeTestPosition>(positionOnly, MergeTestPosition{3.0f, 3.0f});

    Merge<MergeMatch::Any, MergeMapping::Remap,
          MergeTestPosition, MergeTestVelocity>(world, eastl::move(staging));

    // One event for the type, carrying only the entity that actually has it.
    EXPECT_EQ(probe.batchCalls, 1u);
    EXPECT_EQ(probe.singleCalls, 0u);
    ASSERT_EQ(probe.seen.size(), 1u);
    EXPECT_EQ(probe.seen[0], withBoth);

    WorldExecuteContext::Pop();
}

TEST(MergeTest, MergeAnnouncesAfterEverythingMoved)
{
    WorldContext world;
    WorldExecuteContext::Push(world);

    class Probe : public ComponentEventBus::Handler
    {
    public:
        Probe()
        {
            ComponentEventBus::Handler::BusConnect(GetTypeId<MergeTestVelocity>());
        }

        ~Probe() override
        {
            ComponentEventBus::Handler::BusDisconnect();
        }

        void OnComponentsConstruct(eastl::span<const Entity> entities) override
        {
            auto& context = *WorldExecuteContext::Current();
            for (Entity entity : entities)
            {
                // The other type has to be there already, and so has the mapping.
                allPresent = allPresent && context.Has<MergeTestPosition>(entity);
                mappingVisible = mappingVisible && context.Has<MergedFrom<Entity>>(entity);
            }
        }

        bool allPresent = true;
        bool mappingVisible = true;
    } probe;

    StagingContext<Entity> staging;
    Entity entity = staging.CreateEntity();
    staging.Add<MergeTestPosition>(entity, MergeTestPosition{1.0f, 1.0f});
    staging.Add<MergeTestVelocity>(entity, MergeTestVelocity{2.0f, 2.0f});

    Merge<MergeMatch::Any, MergeMapping::Remap,
          MergeTestPosition, MergeTestVelocity>(world, eastl::move(staging));

    EXPECT_TRUE(probe.allPresent);
    EXPECT_TRUE(probe.mappingVisible);

    WorldExecuteContext::Pop();
}

class MergeSceneTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        WorldExecuteContext::Push(world);
        sceneManager = CreateSystem<SceneManager>();
        sceneManager->Init();
    }

    void TearDown() override
    {
        sceneManager.reset();
        WorldExecuteContext::Pop();
        world.Clear();
    }

    SystemUniquePtr<SceneManager> sceneManager;
    WorldContext world;
};

TEST_F(MergeSceneTest, MergePreservesAnAlreadyLinkedTree)
{
    StagingContext<Entity> staging;

    // Authored the way a scene file would be: the links are already consistent.
    Entity parent = staging.CreateEntity();
    Entity child = staging.CreateEntity();
    staging.Add<Hierarchy>(parent, Hierarchy{NullEntity, child, NullEntity, NullEntity});
    staging.Add<Hierarchy>(child, Hierarchy{parent, NullEntity, NullEntity, NullEntity});

    auto result = Merge<MergeMatch::Any, MergeMapping::Remap, Hierarchy>(world, eastl::move(staging));

    // Dispatching one Construct per entity would have failed validation on the child and dropped
    // its Hierarchy.
    ASSERT_EQ(result.created.size(), 2u);
    ASSERT_TRUE(world.Has<Hierarchy>(parent));
    ASSERT_TRUE(world.Has<Hierarchy>(child));
    EXPECT_EQ(world.Get<Hierarchy>(parent).firstChild, child);
    EXPECT_EQ(world.Get<Hierarchy>(child).parent, parent);

    // The root of the batch is the only root, and it is tagged.
    EXPECT_TRUE(world.Has<HierarchyRootTag>(parent));
    EXPECT_FALSE(world.Has<HierarchyRootTag>(child));
}

TEST_F(MergeSceneTest, MergeLinksTheBatchRootUnderAnExistingParent)
{
    auto* scene = Service<IScene>::Get();
    ASSERT_TRUE(scene);

    Entity host = world.CreateEntity();
    scene->AddEntity(host);

    StagingContext<Entity> staging;
    Entity incoming = staging.CreateEntity();
    // Points at an entity outside the batch: a boundary edge that must be linked in for real.
    staging.Add<Hierarchy>(incoming, Hierarchy{host, NullEntity, NullEntity, NullEntity});

    auto result = Merge<MergeMatch::Any, MergeMapping::Remap, Hierarchy>(world, eastl::move(staging));

    ASSERT_EQ(result.created.size(), 1u);
    const Entity merged = result.created[0];
    EXPECT_EQ(world.Get<Hierarchy>(host).firstChild, merged);
    EXPECT_EQ(world.Get<Hierarchy>(merged).parent, host);
    EXPECT_FALSE(world.Has<HierarchyRootTag>(merged));
}

TEST_F(MergeSceneTest, AddEntitiesStillBehaves)
{
    auto* scene = Service<IScene>::Get();
    ASSERT_TRUE(scene);

    eastl::array<Entity, 3> entities;
    world.CreateEntity(entities.begin(), entities.end());
    scene->AddEntities(eastl::span<Entity>(entities.data(), entities.size()));

    EXPECT_EQ(scene->GetEntityCount(), 3u);
    for (Entity entity : entities)
    {
        EXPECT_TRUE(world.Has<HierarchyRootTag>(entity));
    }
}

TEST(MergeTest, CopyLeavesTheSourceIntact)
{
    WorldContext world;
    StagingContext<Entity> prefab;

    Entity source = prefab.CreateEntity();
    prefab.Add<MergeTestPosition>(source, MergeTestPosition{1.0f, 2.0f});

    auto first = Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, prefab);

    ASSERT_EQ(first.created.size(), 1u);
    ASSERT_TRUE(prefab.Has<MergeTestPosition>(source));
    ASSERT_EQ(prefab.Get<MergeTestPosition>(source).x, 1.0f);

    // A prefab source is instantiated more than once.
    auto second = Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, prefab);

    ASSERT_EQ(second.created.size(), 1u);
    ASSERT_NE(second.created[0], first.created[0]);
    ASSERT_EQ(world.Get<MergeTestPosition>(first.created[0]).x, 1.0f);
    ASSERT_EQ(world.Get<MergeTestPosition>(second.created[0]).x, 1.0f);
}

TEST(MergeTest, ExtractCopiesOutOfTheWorld)
{
    WorldContext world;

    Entity a = world.CreateEntity();
    Entity b = world.CreateEntity();
    world.Add<MergeTestPosition>(a, MergeTestPosition{1.0f, 2.0f});
    world.Add<MergeTestPosition>(b, MergeTestPosition{3.0f, 4.0f});

    auto staging = Extract<MergeMatch::Any, MergeTestPosition>(world);

    // The staging context was empty, so every identifier came across verbatim.
    ASSERT_TRUE(staging.Valid(a));
    ASSERT_TRUE(staging.Valid(b));
    ASSERT_EQ(staging.Get<MergeTestPosition>(a).x, 1.0f);
    ASSERT_EQ(staging.Get<MergeTestPosition>(b).x, 3.0f);

    // And the world kept everything.
    ASSERT_EQ(world.Get<MergeTestPosition>(a).x, 1.0f);
    ASSERT_EQ(world.GetView<MergeTestPosition>().size(), 2u);
}

TEST(MergeTest, ExtractIgnoresTypesAbsentFromTheWorld)
{
    WorldContext world;

    Entity entity = world.CreateEntity();
    world.Add<MergeTestPosition>(entity, MergeTestPosition{1.0f, 2.0f});

    auto staging = Extract<MergeMatch::Any, MergeTestPosition, MergeTestTag>(world);

    ASSERT_TRUE(staging.Has<MergeTestPosition>(entity));
    ASSERT_FALSE(staging.Has<MergeTestTag>(entity));
}

TEST(MergeTest, ExtractAllSkipsPartialEntities)
{
    WorldContext world;

    Entity both = world.CreateEntity();
    Entity partial = world.CreateEntity();
    world.Add<MergeTestPosition>(both, MergeTestPosition{1.0f, 1.0f});
    world.Add<MergeTestVelocity>(both, MergeTestVelocity{2.0f, 2.0f});
    world.Add<MergeTestPosition>(partial, MergeTestPosition{9.0f, 9.0f});

    auto staging = Extract<MergeMatch::All, MergeTestPosition, MergeTestVelocity>(world);

    ASSERT_TRUE(staging.Valid(both));
    ASSERT_FALSE(staging.Valid(partial));
    ASSERT_EQ(staging.GetView<MergeTestPosition>().size(), 1u);
}

TEST(MergeTest, ExtractThenMergeRoundTrips)
{
    WorldContext world;

    Entity original = world.CreateEntity();
    world.Add<MergeTestPosition>(original, MergeTestPosition{5.0f, 6.0f});

    // Copy to the clipboard, then paste: every identifier collides, so the paste lands elsewhere.
    auto clipboard = Extract<MergeMatch::Any, MergeTestPosition>(world);
    auto pasted = Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, eastl::move(clipboard));

    ASSERT_EQ(pasted.created.size(), 1u);
    ASSERT_NE(pasted.created[0], original);
    ASSERT_EQ(world.Get<MergeTestPosition>(pasted.created[0]).x, 5.0f);
    ASSERT_EQ(world.Get<MergeTestPosition>(original).x, 5.0f);
}

TEST_F(MergeSceneTest, SetParentBuildsATreeInStaging)
{
    auto* scene = Service<IScene>::Get();
    ASSERT_TRUE(scene);

    StagingContext<Entity> staging;
    Entity root = staging.CreateEntity();
    Entity first = staging.CreateEntity();
    Entity second = staging.CreateEntity();

    // Nothing watches a staging context, so SetParent has to finish the job itself.
    scene->SetParent(staging, first, root);
    scene->SetParent(staging, second, root);

    // Prepends, same as the world overload does when no prevSibling is given.
    EXPECT_EQ(staging.Get<Hierarchy>(root).firstChild, second);
    EXPECT_EQ(staging.Get<Hierarchy>(second).nextSibling, first);
    EXPECT_EQ(staging.Get<Hierarchy>(first).prevSibling, second);
    EXPECT_EQ(staging.Get<Hierarchy>(first).parent, root);

    // A staging context carries no root tags; the world adds them on arrival.
    EXPECT_FALSE(staging.Has<HierarchyRootTag>(root));

    auto result = Merge<MergeMatch::Any, MergeMapping::Remap, Hierarchy>(world, eastl::move(staging));

    ASSERT_EQ(result.created.size(), 3u);
    EXPECT_EQ(world.Get<Hierarchy>(root).firstChild, second);
    EXPECT_EQ(world.Get<Hierarchy>(second).nextSibling, first);
    EXPECT_EQ(world.Get<Hierarchy>(first).parent, root);
    EXPECT_TRUE(world.Has<HierarchyRootTag>(root));
    EXPECT_FALSE(world.Has<HierarchyRootTag>(first));
    EXPECT_EQ(scene->GetEntityCount(), 3u);
}

TEST_F(MergeSceneTest, SetParentOnStagingMovesAnExistingChild)
{
    auto* scene = Service<IScene>::Get();
    ASSERT_TRUE(scene);

    StagingContext<Entity> staging;
    Entity firstParent = staging.CreateEntity();
    Entity secondParent = staging.CreateEntity();
    Entity child = staging.CreateEntity();

    scene->SetParent(staging, child, firstParent);
    scene->SetParent(staging, child, secondParent);

    EXPECT_EQ(staging.Get<Hierarchy>(child).parent, secondParent);
    EXPECT_EQ(staging.Get<Hierarchy>(secondParent).firstChild, child);
    EXPECT_EQ(staging.Get<Hierarchy>(firstParent).firstChild, NullEntity);
}

TEST_F(MergeSceneTest, SetParentOnStagingRejectsAForeignPrevSibling)
{
    auto* scene = Service<IScene>::Get();
    ASSERT_TRUE(scene);

    StagingContext<Entity> staging;
    Entity parentA = staging.CreateEntity();
    Entity parentB = staging.CreateEntity();
    Entity childOfB = staging.CreateEntity();
    Entity newcomer = staging.CreateEntity();

    scene->SetParent(staging, childOfB, parentB);

    // childOfB belongs to parentB, so it cannot be a sibling under parentA.
    scene->SetParent(staging, newcomer, parentA, childOfB);

    EXPECT_FALSE(staging.Has<Hierarchy>(newcomer));
    EXPECT_EQ(staging.Get<Hierarchy>(parentA).firstChild, NullEntity);
    EXPECT_EQ(staging.Get<Hierarchy>(parentB).firstChild, childOfB);
    EXPECT_EQ(staging.Get<Hierarchy>(childOfB).nextSibling, NullEntity);
}
