#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <ECS/Common.h>
#include <ECS/Entity.h>
#include <ECS/WorldContext.h>
#include <ECS/ContextStorage.h>
#include <ECS/StagingContext.h>
#include <ECS/Merge/ContextMerge.h>
#include <Service/Service.h>
#include <Hierarchy/HierarchyComponent.h>
#include <Hierarchy/IHierarchy.h>
#include <Hierarchy/HierarchyManager.h>
#include <CoreComponents/Name.h>
#include <ECS/ComponentRuntime.h>
#include <Reflection/TypeRegistry.h>

#include <EASTL/algorithm.h>
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

    struct MergeTestLink
    {
        Entity target{NullEntity};
    };

    /// Destroy and recreate the same slot until its version has moved on.
    //! The batch a merge just landed, read from the records it keeps when asked to.
    template<typename Context>
    eastl::vector<typename Context::Entity> MergedBatch(Context& context)
    {
        using E = typename Context::Entity;

        eastl::vector<E> entities;
        for (E entity : context.template GetView<MergedFrom<E>>())
        {
            entities.push_back(entity);
        }
        ClearMergeRecords(context);
        return entities;
    }

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

    SPARK_COMPONENT_TRAITS(MergeTestLink,
        static constexpr auto entityRefs = EntityRefs<&MergeTestLink::target>;
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

    Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    // The target was empty, so every hint was honoured. created is in storage order, which is not
    // insertion order -- the merge promises no ordering.
    ASSERT_EQ(result.size(), 2u);
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

    Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    ASSERT_EQ(result.size(), 1u);
    ASSERT_NE(result[0], source);
    ASSERT_EQ(world.Get<MergeTestPosition>(result[0]).x, 5.0f);

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

    Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    // Identity said the slot was free, occupancy said otherwise -- the component must not land on
    // the occupant.
    ASSERT_EQ(result.size(), 1u);
    ASSERT_NE(result[0], source);
    ASSERT_FALSE(world.Has<MergeTestPosition>(occupant));
    ASSERT_EQ(world.Get<MergeTestPosition>(result[0]).x, 7.0f);
}

TEST(MergeTest, MergeVisitsAnEntityOnceAcrossTypes)
{
    WorldContext world;
    StagingContext<Entity> staging;

    Entity both = staging.CreateEntity();
    staging.Add<MergeTestPosition>(both, MergeTestPosition{1.0f, 1.0f});
    staging.Add<MergeTestVelocity>(both, MergeTestVelocity{2.0f, 2.0f});

    Merge<MergeMatch::Any, MergeMapping::Remap,
                        MergeTestPosition, MergeTestVelocity>(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    ASSERT_EQ(result.size(), 1u);
    ASSERT_TRUE(world.Has<MergeTestPosition>(result[0]));
    ASSERT_TRUE(world.Has<MergeTestVelocity>(result[0]));
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

    Merge<MergeMatch::All, MergeMapping::Remap,
                        MergeTestPosition, MergeTestVelocity>(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(world.Get<MergeTestPosition>(result[0]).x, 1.0f);
    ASSERT_EQ(world.GetView<MergeTestPosition>().size(), 1u);
}

TEST(MergeTest, MergeCarriesTagComponents)
{
    WorldContext world;
    StagingContext<Entity> staging;

    Entity entity = staging.CreateEntity();
    staging.Add<MergeTestTag>(entity);

    Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestTag>(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    ASSERT_EQ(result.size(), 1u);
    ASSERT_TRUE(world.Has<MergeTestTag>(result[0]));
}

TEST(MergeTest, MergeIgnoresTypesAbsentFromSource)
{
    WorldContext world;
    StagingContext<Entity> staging;

    Entity entity = staging.CreateEntity();
    staging.Add<MergeTestPosition>(entity, MergeTestPosition{1.0f, 2.0f});

    Merge<MergeMatch::Any, MergeMapping::Remap,
                        MergeTestPosition, MergeTestVelocity>(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    ASSERT_EQ(result.size(), 1u);
    ASSERT_FALSE(world.Has<MergeTestVelocity>(result[0]));
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
        hierarchyManager = CreateSystem<HierarchyManager>();
        hierarchyManager->Init();
    }

    void TearDown() override
    {
        hierarchyManager.reset();
        WorldExecuteContext::Pop();
        world.Clear();
    }

    SystemUniquePtr<HierarchyManager> hierarchyManager;
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

    Merge<MergeMatch::Any, MergeMapping::Remap, Hierarchy>(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    // Dispatching one Construct per entity would have failed validation on the child and dropped
    // its Hierarchy.
    ASSERT_EQ(result.size(), 2u);
    ASSERT_TRUE(world.Has<Hierarchy>(parent));
    ASSERT_TRUE(world.Has<Hierarchy>(child));
    EXPECT_EQ(world.Get<Hierarchy>(parent).firstChild, child);
    EXPECT_EQ(world.Get<Hierarchy>(child).parent, parent);

    // The root of the batch is the only root, and it is tagged.
    EXPECT_TRUE(world.Has<HierarchyRootTag>(parent));
    EXPECT_FALSE(world.Has<HierarchyRootTag>(child));
}

TEST_F(MergeSceneTest, TheBatchIsLinkedUnderAnExistingParentAfterTheMerge)
{
    auto* hierarchy = Service<IHierarchy>::Get();
    ASSERT_TRUE(hierarchy);

    Entity host = world.CreateEntity();
    hierarchy->AddEntity(host);

    StagingContext<Entity> staging;
    Entity incoming = staging.CreateEntity();
    staging.Add<Hierarchy>(incoming);

    Merge<MergeMatch::Any, MergeMapping::Remap, Hierarchy>(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    ASSERT_EQ(result.size(), 1u);
    const Entity merged = result[0];
    EXPECT_TRUE(world.Has<HierarchyRootTag>(merged));

    // A live entity cannot be named from inside staging, so the boundary edge is made here.
    hierarchy->SetParent(merged, host);

    EXPECT_EQ(world.Get<Hierarchy>(host).firstChild, merged);
    EXPECT_EQ(world.Get<Hierarchy>(merged).parent, host);
    EXPECT_FALSE(world.Has<HierarchyRootTag>(merged));
}

TEST_F(MergeSceneTest, AddEntitiesStillBehaves)
{
    auto* hierarchy = Service<IHierarchy>::Get();
    ASSERT_TRUE(hierarchy);

    eastl::array<Entity, 3> entities;
    world.CreateEntity(entities.begin(), entities.end());
    hierarchy->AddEntities(eastl::span<Entity>(entities.data(), entities.size()));

    EXPECT_EQ(hierarchy->GetEntityCount(), 3u);
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

    Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, prefab, MergeRecords::Keep);
    const auto first = MergedBatch(world);

    ASSERT_EQ(first.size(), 1u);
    ASSERT_TRUE(prefab.Has<MergeTestPosition>(source));
    ASSERT_EQ(prefab.Get<MergeTestPosition>(source).x, 1.0f);

    // A prefab source is instantiated more than once.
    Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, prefab, MergeRecords::Keep);
    const auto second = MergedBatch(world);

    ASSERT_EQ(second.size(), 1u);
    ASSERT_NE(second[0], first[0]);
    ASSERT_EQ(world.Get<MergeTestPosition>(first[0]).x, 1.0f);
    ASSERT_EQ(world.Get<MergeTestPosition>(second[0]).x, 1.0f);
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
    Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestPosition>(world, eastl::move(clipboard), MergeRecords::Keep);
    const auto pasted = MergedBatch(world);

    ASSERT_EQ(pasted.size(), 1u);
    ASSERT_NE(pasted[0], original);
    ASSERT_EQ(world.Get<MergeTestPosition>(pasted[0]).x, 5.0f);
    ASSERT_EQ(world.Get<MergeTestPosition>(original).x, 5.0f);
}

TEST_F(MergeSceneTest, SetParentBuildsATreeInStaging)
{
    auto* hierarchy = Service<IHierarchy>::Get();
    ASSERT_TRUE(hierarchy);

    StagingContext<Entity> staging;
    Entity root = staging.CreateEntity();
    Entity first = staging.CreateEntity();
    Entity second = staging.CreateEntity();

    // Nothing watches a staging context, so SetParent has to finish the job itself.
    hierarchy->SetParent(staging, first, root);
    hierarchy->SetParent(staging, second, root);

    // Prepends, same as the world overload does when no prevSibling is given.
    EXPECT_EQ(staging.Get<Hierarchy>(root).firstChild, second);
    EXPECT_EQ(staging.Get<Hierarchy>(second).nextSibling, first);
    EXPECT_EQ(staging.Get<Hierarchy>(first).prevSibling, second);
    EXPECT_EQ(staging.Get<Hierarchy>(first).parent, root);

    // A staging context carries no root tags; the world adds them on arrival.
    EXPECT_FALSE(staging.Has<HierarchyRootTag>(root));

    Merge<MergeMatch::Any, MergeMapping::Remap, Hierarchy>(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(world.Get<Hierarchy>(root).firstChild, second);
    EXPECT_EQ(world.Get<Hierarchy>(second).nextSibling, first);
    EXPECT_EQ(world.Get<Hierarchy>(first).parent, root);
    EXPECT_TRUE(world.Has<HierarchyRootTag>(root));
    EXPECT_FALSE(world.Has<HierarchyRootTag>(first));
    EXPECT_EQ(hierarchy->GetEntityCount(), 3u);
}

TEST_F(MergeSceneTest, SetParentOnStagingMovesAnExistingChild)
{
    auto* hierarchy = Service<IHierarchy>::Get();
    ASSERT_TRUE(hierarchy);

    StagingContext<Entity> staging;
    Entity firstParent = staging.CreateEntity();
    Entity secondParent = staging.CreateEntity();
    Entity child = staging.CreateEntity();

    hierarchy->SetParent(staging, child, firstParent);
    hierarchy->SetParent(staging, child, secondParent);

    EXPECT_EQ(staging.Get<Hierarchy>(child).parent, secondParent);
    EXPECT_EQ(staging.Get<Hierarchy>(secondParent).firstChild, child);
    EXPECT_EQ(staging.Get<Hierarchy>(firstParent).firstChild, NullEntity);
}

TEST_F(MergeSceneTest, SetParentOnStagingRejectsAForeignPrevSibling)
{
    auto* hierarchy = Service<IHierarchy>::Get();
    ASSERT_TRUE(hierarchy);

    StagingContext<Entity> staging;
    Entity parentA = staging.CreateEntity();
    Entity parentB = staging.CreateEntity();
    Entity childOfB = staging.CreateEntity();
    Entity newcomer = staging.CreateEntity();

    hierarchy->SetParent(staging, childOfB, parentB);

    // childOfB belongs to parentB, so it cannot be a sibling under parentA.
    hierarchy->SetParent(staging, newcomer, parentA, childOfB);

    EXPECT_FALSE(staging.Has<Hierarchy>(newcomer));
    EXPECT_EQ(staging.Get<Hierarchy>(parentA).firstChild, NullEntity);
    EXPECT_EQ(staging.Get<Hierarchy>(parentB).firstChild, childOfB);
    EXPECT_EQ(staging.Get<Hierarchy>(childOfB).nextSibling, NullEntity);
}

TEST_F(MergeSceneTest, MergeATreeIntoANonEmptyWorld)
{
    // Something already occupies the identifiers the staging tree will ask for.
    world.CreateEntity();
    world.CreateEntity();

    StagingContext<Entity> staging;
    Entity parent = staging.CreateEntity();
    Entity child = staging.CreateEntity();
    staging.Add<Hierarchy>(parent, Hierarchy{NullEntity, child, NullEntity, NullEntity});
    staging.Add<Hierarchy>(child, Hierarchy{parent, NullEntity, NullEntity, NullEntity});

    Merge<MergeMatch::Any, MergeMapping::Remap, Hierarchy>(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    ASSERT_EQ(result.size(), 2u);
    const Entity first = result[0];
    const Entity second = result[1];
    EXPECT_NE(first, parent);
    EXPECT_NE(second, parent);

    // Untranslated links name entities that are not in the scene, and validation drops the
    // component on arrival -- so surviving at all is most of the point.
    ASSERT_TRUE(world.Has<Hierarchy>(first));
    ASSERT_TRUE(world.Has<Hierarchy>(second));

    // Merge promises no ordering, so the two are told apart by shape.
    const Entity root = world.Get<Hierarchy>(first).parent == NullEntity ? first : second;
    const Entity leaf = root == first ? second : first;

    EXPECT_EQ(world.Get<Hierarchy>(root).firstChild, leaf);
    EXPECT_EQ(world.Get<Hierarchy>(leaf).parent, root);
    EXPECT_TRUE(world.Has<HierarchyRootTag>(root));
    EXPECT_FALSE(world.Has<HierarchyRootTag>(leaf));
}

TEST_F(MergeSceneTest, ReparentingKeepsExistingChildrenInStaging)
{
    auto* hierarchy = Service<IHierarchy>::Get();
    ASSERT_TRUE(hierarchy);

    StagingContext<Entity> staging;
    Entity root = staging.CreateEntity();
    Entity node = staging.CreateEntity();
    Entity prim = staging.CreateEntity();

    hierarchy->SetParent(staging, prim, node);
    hierarchy->SetParent(staging, node, root);

    EXPECT_EQ(staging.Get<Hierarchy>(node).firstChild, prim);
    EXPECT_EQ(staging.Get<Hierarchy>(prim).parent, node);
}

TEST_F(MergeSceneTest, ReparentingKeepsExistingChildrenInTheWorld)
{
    auto* hierarchy = Service<IHierarchy>::Get();
    ASSERT_TRUE(hierarchy);

    Entity root = world.CreateEntity();
    Entity node = world.CreateEntity();
    Entity prim = world.CreateEntity();
    hierarchy->AddEntity(root);
    hierarchy->AddEntity(node);
    hierarchy->AddEntity(prim);

    hierarchy->SetParent(prim, node);
    hierarchy->SetParent(node, root);

    EXPECT_EQ(world.Get<Hierarchy>(node).firstChild, prim);
    EXPECT_EQ(world.Get<Hierarchy>(prim).parent, node);
}

// The shape SpawnModel builds: a node tree whose leaves are the primitives of a mesh, authored in
// the same two passes -- primitives first, node-to-node links after.
TEST_F(MergeSceneTest, MergeAModelShapedTree)
{
    auto* hierarchy = Service<IHierarchy>::Get();
    ASSERT_TRUE(hierarchy);

    world.CreateEntity();

    StagingContext<Entity> staging;
    Entity rootNode = staging.CreateEntity();
    Entity childNode = staging.CreateEntity();
    Entity prim = staging.CreateEntity();
    staging.Add<Name>(rootNode, eastl::string("root"));
    staging.Add<Name>(childNode, eastl::string("child"));
    staging.Add<Name>(prim, eastl::string("prim"));
    staging.Add<Hierarchy>(rootNode);
    staging.Add<Hierarchy>(childNode);

    hierarchy->SetParent(staging, prim, childNode);
    hierarchy->SetParent(staging, childNode, rootNode);

    Merge<MergeMatch::Any, MergeMapping::Remap, Name, Hierarchy>(
        world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);
    ASSERT_EQ(result.size(), 3u);

    auto byName = [&](const char* name)
    {
        for (Entity entity : result)
        {
            if (world.Has<Name>(entity) && world.Get<Name>(entity).name == name)
            {
                return entity;
            }
        }
        return NullEntity;
    };

    const Entity root = byName("root");
    const Entity child = byName("child");
    const Entity leaf = byName("prim");
    ASSERT_NE(root, NullEntity);
    ASSERT_NE(child, NullEntity);
    ASSERT_NE(leaf, NullEntity);

    EXPECT_EQ(world.Get<Hierarchy>(root).firstChild, child);
    EXPECT_EQ(world.Get<Hierarchy>(child).parent, root);
    EXPECT_EQ(world.Get<Hierarchy>(child).firstChild, leaf);
    EXPECT_EQ(world.Get<Hierarchy>(leaf).parent, child);

    EXPECT_TRUE(world.Has<HierarchyRootTag>(root));
    EXPECT_FALSE(world.Has<HierarchyRootTag>(child));
    EXPECT_FALSE(world.Has<HierarchyRootTag>(leaf));
}

TEST(MergeTest, TranslateRewritesAReferenceToAParticipant)
{
    WorldContext world;
    world.CreateEntity();
    world.CreateEntity();

    StagingContext<Entity> staging;
    Entity head = staging.CreateEntity();
    Entity tail = staging.CreateEntity();
    staging.Add<MergeTestLink>(head, MergeTestLink{tail});
    staging.Add<MergeTestLink>(tail, MergeTestLink{NullEntity});

    Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestLink>(
        world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    ASSERT_EQ(result.size(), 2u);
    const Entity first = result[0];
    const Entity second = result[1];
    ASSERT_NE(first, head);

    // Whichever of the two is the head, its reference names the other one.
    const Entity linked = world.Get<MergeTestLink>(first).target != NullEntity ? first : second;
    const Entity other = linked == first ? second : first;
    EXPECT_EQ(world.Get<MergeTestLink>(linked).target, other);
    EXPECT_EQ(world.Get<MergeTestLink>(other).target, NullEntity);
}

TEST(MergeTest, TranslateDropsAReferenceToANonParticipant)
{
    WorldContext world;
    StagingContext<Entity> staging;

    Entity head = staging.CreateEntity();
    Entity outsider = staging.CreateEntity();
    staging.Add<MergeTestLink>(head, MergeTestLink{outsider});
    // The outsider carries nothing that is being merged, so it never reaches the world.
    staging.Add<MergeTestPosition>(outsider, MergeTestPosition{1.0f, 2.0f});

    Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestLink>(
        world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(world.Get<MergeTestLink>(result[0]).target, NullEntity);
}

TEST(MergeTest, CopyInstantiatesAPrefabWithItsOwnReferences)
{
    WorldContext world;
    StagingContext<Entity> prefab;

    Entity head = prefab.CreateEntity();
    Entity tail = prefab.CreateEntity();
    prefab.Add<MergeTestLink>(head, MergeTestLink{tail});
    prefab.Add<MergeTestLink>(tail, MergeTestLink{NullEntity});

    Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestLink>(world, prefab, MergeRecords::Keep);
    const auto first = MergedBatch(world);
    Merge<MergeMatch::Any, MergeMapping::Remap, MergeTestLink>(world, prefab, MergeRecords::Keep);
    const auto second = MergedBatch(world);

    ASSERT_EQ(first.size(), 2u);
    ASSERT_EQ(second.size(), 2u);

    // Translation happens on the copy that landed, so the prefab can be instantiated again.
    EXPECT_EQ(prefab.Get<MergeTestLink>(head).target, tail);

    auto pointsInsideItself = [&](const eastl::vector<Entity>& instance)
    {
        size_t linkCount = 0;
        for (Entity entity : instance)
        {
            const Entity target = world.Get<MergeTestLink>(entity).target;
            if (target == NullEntity)
            {
                continue;
            }
            ++linkCount;
            EXPECT_NE(eastl::find(instance.begin(), instance.end(), target),
                instance.end());
        }
        EXPECT_EQ(linkCount, 1u);
    };

    pointsInsideItself(first);
    pointsInsideItself(second);

    for (Entity entity : second)
    {
        EXPECT_EQ(eastl::find(first.begin(), first.end(), entity),
            first.end());
    }
}

namespace
{
    //! The runtime path reads the reflected type, so these tests have to register one. The
    //! global context is filled once and reused by every test in this file.
    void RegisterRuntimeTestTypes()
    {
        static bool registered = false;
        if (registered)
        {
            return;
        }
        registered = true;

        ReflectContext& context = TypeRegistry::GetContext();
        context.Reflect<MergeTestPosition>().Type("MergeTestPosition")
            .Data<&MergeTestPosition::x>("x")
            .Data<&MergeTestPosition::y>("y");
        ComponentRuntime<MergeTestPosition>(context);

        context.Reflect<MergeTestTag>().Type("MergeTestTag");
        ComponentRuntime<MergeTestTag>(context);

        context.Reflect<MergeTestLink>().Type("MergeTestLink")
            .Data<&MergeTestLink::target>("Target");
        ComponentRuntime<MergeTestLink>(context);

        // Reflected but never registered for runtime data: the merge has to drop it.
        context.Reflect<MergeTestVelocity>().Type("MergeTestVelocity")
            .Data<&MergeTestVelocity::dx>("dx")
            .Data<&MergeTestVelocity::dy>("dy");
    }

    MetaAny MakeValue(const MetaType& type)
    {
        return type.construct();
    }
}

TEST(RuntimeMergeTest, TakesItsTypesFromTheSource)
{
    RegisterRuntimeTestTypes();

    StagingContext<Entity> staging;
    const Entity first  = staging.CreateEntity();
    const Entity second = staging.CreateEntity();
    staging.Add<MergeTestPosition>(first, MergeTestPosition{1.f, 2.f});
    staging.Add<MergeTestTag>(second);

    WorldContext world;
    Merge(world, eastl::move(staging), MergeRecords::Keep);

    // The target was empty, so both identifiers were kept.
    EXPECT_EQ(MergedEntity(world, first), first);
    EXPECT_EQ(MergedEntity(world, second), second);

    const auto batch = MergedBatch(world);
    EXPECT_EQ(batch.size(), 2u);
    ASSERT_TRUE(world.Valid(first));
    ASSERT_TRUE(world.Valid(second));
    EXPECT_EQ(world.Get<MergeTestPosition>(first).x, 1.f);
    EXPECT_TRUE(world.Has<MergeTestTag>(second));
}

TEST(RuntimeMergeTest, RenumbersAndTranslatesReferences)
{
    RegisterRuntimeTestTypes();

    // The identifiers the batch wants are taken, so every one of them moves.
    WorldContext world;
    eastl::array<Entity, 4> occupants{};
    for (Entity& occupant : occupants)
    {
        occupant = world.CreateEntity();
    }

    StagingContext<Entity> staging;
    const Entity head = staging.CreateEntity(occupants[1]);
    const Entity tail = staging.CreateEntity(occupants[2]);
    staging.Add<MergeTestLink>(head, MergeTestLink{tail});
    staging.Add<MergeTestLink>(tail, MergeTestLink{NullEntity});

    Merge(world, eastl::move(staging), MergeRecords::Keep);

    // The records answer where each source identifier went.
    const Entity landedHead = MergedEntity(world, head);
    const Entity landedTail = MergedEntity(world, tail);
    EXPECT_EQ(MergedBatch(world).size(), 2u);

    EXPECT_NE(landedHead, head);
    EXPECT_NE(landedTail, tail);
    EXPECT_EQ(world.Get<MergeTestLink>(landedHead).target, landedTail);
    EXPECT_EQ(world.Get<MergeTestLink>(landedTail).target, NullEntity);
}

TEST(RuntimeMergeTest, DropsAReferenceThatLeavesTheBatch)
{
    RegisterRuntimeTestTypes();

    // The outsider has to sit on an identifier the batch never mentions -- a staging entity
    // that happens to share an identifier with it would be part of the batch, not outside it.
    WorldContext world;
    eastl::array<Entity, 8> filler{};
    for (Entity& entity : filler)
    {
        entity = world.CreateEntity();
    }
    const Entity outsider = filler[7];

    StagingContext<Entity> staging;
    const Entity linker = staging.CreateEntity();
    staging.Add<MergeTestLink>(linker, MergeTestLink{outsider});

    Merge(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(world.Get<MergeTestLink>(result[0]).target, NullEntity);
}

TEST(RuntimeMergeTest, DropsAComponentWithNoRuntimeBinding)
{
    RegisterRuntimeTestTypes();

    StagingContext<Entity> staging;
    const Entity entity = staging.CreateEntity();
    staging.Add<MergeTestPosition>(entity, MergeTestPosition{3.f, 4.f});
    staging.Add<MergeTestVelocity>(entity, MergeTestVelocity{5.f, 6.f});

    WorldContext world;
    Merge(world, eastl::move(staging), MergeRecords::Keep);
    const auto result = MergedBatch(world);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_TRUE(world.Has<MergeTestPosition>(entity));
    EXPECT_FALSE(world.Has<MergeTestVelocity>(entity));
}

TEST(RuntimeAddTest, AddsAComponentDescribedAtRuntime)
{
    RegisterRuntimeTestTypes();

    const MetaType type = TypeRegistry::GetContext().Resolve<MergeTestPosition>();
    MetaAny value = MakeValue(type);
    ASSERT_TRUE(value);
    value.cast<MergeTestPosition&>().x = 7.f;

    StagingContext<Entity> staging;
    const Entity entity = staging.CreateEntity();

    EXPECT_TRUE(staging.Add(entity, value));
    ASSERT_TRUE(staging.Has<MergeTestPosition>(entity));
    EXPECT_EQ(staging.Get<MergeTestPosition>(entity).x, 7.f);

    // A zero-field component is the type itself; there is no instance to copy.
    const MetaType tagType = TypeRegistry::GetContext().Resolve<MergeTestTag>();
    EXPECT_TRUE(staging.Add(entity, MakeValue(tagType)));
    EXPECT_TRUE(staging.Has<MergeTestTag>(entity));
}

TEST(RuntimeAddTest, RefusesWhatItCannotPlace)
{
    RegisterRuntimeTestTypes();

    StagingContext<Entity> staging;
    const Entity entity = staging.CreateEntity();

    const MetaType type = TypeRegistry::GetContext().Resolve<MergeTestPosition>();
    EXPECT_TRUE(staging.Add(entity, MakeValue(type)));
    // Twice is undefined for entt, so it has to be refused here.
    EXPECT_FALSE(staging.Add(entity, MakeValue(type)));

    // Reflected, but never registered for runtime data.
    const MetaType unbound = TypeRegistry::GetContext().Resolve<MergeTestVelocity>();
    EXPECT_FALSE(staging.Add(entity, MakeValue(unbound)));

    // No such entity.
    StagingContext<Entity> empty;
    EXPECT_FALSE(empty.Add(entity, MakeValue(type)));

    EXPECT_FALSE(staging.Add(entity, MetaAny{}));
}

namespace
{
    //! A second context, so the runtime path is exercised where nothing observes the target
    //! and the component belongs to another entity type than the world's.
    enum class MergeTestHandle : uint32_t
    {
    };

    struct MergeTestSlot
    {
        MergeTestHandle next{MergeTestHandle{entt::null}};
        int32_t         value{0};
    };
}

namespace Spark
{
    template<>
    struct EntityTraits<MergeTestHandle>
    {
        using value_type = MergeTestHandle;
        using entity_type = uint32_t;
        using version_type = uint16_t;

        static constexpr entity_type entity_mask = 0xFFFFF;
        static constexpr entity_type version_mask = 0xFFF;
    };

    template<>
    struct ComponentTraits<MergeTestSlot> : ComponentTraitsBase<MergeTestSlot, MergeTestHandle>
    {
        static constexpr auto entityRefs = EntityRefs<&MergeTestSlot::next>;
    };
}

namespace entt
{
    template<>
    struct entt_traits<MergeTestHandle> : basic_entt_traits<Spark::EntityTraits<MergeTestHandle>>
    {
        using base_type = basic_entt_traits<Spark::EntityTraits<MergeTestHandle>>;
        static constexpr std::size_t page_size = ENTT_SPARSE_PAGE;
    };
}

TEST(RuntimeMergeTest, WorksInAContextOfAnotherEntityType)
{
    static bool registered = false;
    if (!registered)
    {
        registered = true;
        ReflectContext& context = TypeRegistry::GetContext();
        context.Reflect<MergeTestSlot>().Type("MergeTestSlot")
            .Data<&MergeTestSlot::next>("Next")
            .Data<&MergeTestSlot::value>("Value");
        ComponentRuntime<MergeTestSlot>(context);
    }

    BasicContext<MergeTestHandle> live;
    const MergeTestHandle occupant = live.CreateEntity();

    StagingContext<MergeTestHandle> staging;
    const MergeTestHandle first  = staging.CreateEntity(occupant);
    const MergeTestHandle second = staging.CreateEntity();
    staging.Add<MergeTestSlot>(first, MergeTestSlot{second, 1});
    staging.Add<MergeTestSlot>(second, MergeTestSlot{MergeTestHandle{entt::null}, 2});

    Merge(live, eastl::move(staging), MergeRecords::Keep);

    const MergeTestHandle landedFirst  = MergedEntity(live, first);
    const MergeTestHandle landedSecond = MergedEntity(live, second);
    EXPECT_EQ(MergedBatch(live).size(), 2u);

    // The occupied identifier moved, the free one was kept, and the reference followed.
    EXPECT_NE(landedFirst, first);
    EXPECT_EQ(landedSecond, second);
    EXPECT_EQ(live.Get<MergeTestSlot>(landedFirst).next, landedSecond);
}
