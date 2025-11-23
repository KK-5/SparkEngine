#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <gtest/gtest.h>

#include <ECS/Entity.h>
#include <ECS/WorldContext.h>
#include <ECS/Tag.h>
#include <ECS/ISystem.h>
#include <Service/Service.h>
#include <Log/SpdLogSystem.h>

#include <iostream>

using namespace Spark;

TEST(ECSTest, CreateDestoryEntity)
{
    WorldContext wordContext;

    Entity ent = wordContext.CreateEntity();
    ASSERT_TRUE(wordContext.Valid(ent));

    Entity namedEntity = wordContext.CreateEntity("MyName");
    ASSERT_TRUE(wordContext.Valid(namedEntity));
    ASSERT_EQ(wordContext.Get<Name>(namedEntity).m_name, "MyName");

    wordContext.DestoryEntity(ent);
    wordContext.DestoryEntity(namedEntity);
}

struct Position
{
    float x;
    float y;
};

struct Velocity 
{ 
    float dx;
    float dy; 
};

TEST(ECSTest, Component)
{
    WorldContext wordContext;

    Entity ent = wordContext.CreateEntity();
    wordContext.Add<Position>(ent, 2.f, 2.f);
    ASSERT_TRUE(wordContext.HasAny<Position>(ent));
    ASSERT_FALSE(wordContext.HasAny<Velocity>(ent));
    ASSERT_EQ(wordContext.TryGet<Velocity>(ent), nullptr);

    Velocity velocity{0.2f, 0.1f};
    wordContext.Add<Velocity>(ent, velocity);
    ASSERT_TRUE(wordContext.HasAny<Velocity>(ent));
    ASSERT_TRUE((wordContext.HasAll<Position, Velocity>(ent)));

    auto pos = wordContext.Get<Position>(ent);
    ASSERT_FLOAT_EQ(pos.x, 2.f);
    ASSERT_FLOAT_EQ(pos.y, 2.f);

    auto [position_, velocity_] = wordContext.Get<Position, Velocity>(ent);
    position_.x += velocity_.dx;
    position_.y += velocity_.dy;

    auto newPos = wordContext.Get<Position>(ent);
    ASSERT_FLOAT_EQ(newPos.x, 2.2f);
    ASSERT_FLOAT_EQ(newPos.y, 2.1f);

    auto posPtr = wordContext.TryGet<Position>(ent);
    ASSERT_NE(posPtr, nullptr);
    ASSERT_FLOAT_EQ(posPtr->x, 2.2f);
    ASSERT_FLOAT_EQ(posPtr->y, 2.1f);

    ASSERT_EQ((wordContext.Remove<Position, Velocity>(ent)), 2u);
    ASSERT_FALSE((wordContext.HasAny<Position, Velocity>(ent)));
}

TEST(ECSTest, ViewAndGroup)
{
    WorldContext wordContext;

    Entity ent1 = wordContext.CreateEntity();
    Entity ent2 = wordContext.CreateEntity();
    Entity ent3 = wordContext.CreateEntity();

    wordContext.Add<Position>(ent1, 2.f, 2.f);
    wordContext.Add<Velocity>(ent2, 0.1f, 0.1f);
    wordContext.Add<Position>(ent3, 3.f, 3.f);
    wordContext.Add<Velocity>(ent3, 0.2f, 0.4f);

    auto view = wordContext.GetView<Position, Velocity>();
    view.each([&](Entity entity, Position& pos, Velocity& vel)
        {
            pos.x += vel.dx;
            pos.y += vel.dy;
        }
    );
    ASSERT_FLOAT_EQ(wordContext.Get<Position>(ent3).x, 3.2f);
    ASSERT_FLOAT_EQ(wordContext.Get<Position>(ent3).y, 3.4f);

    auto singleView = wordContext.GetView<Position>();
    ASSERT_FLOAT_EQ(singleView[ent1].x, 2.f);
    ASSERT_FLOAT_EQ(singleView[ent3].x, 3.2f);

    auto excludeView = wordContext.GetView<Position>(Exclude<Velocity>);
    excludeView.each([&](Position& pos)
        {
            pos.x += 1.f;
        }
    );
    ASSERT_FLOAT_EQ(wordContext.Get<Position>(ent1).x, 3.f);
    ASSERT_FLOAT_EQ(wordContext.Get<Position>(ent3).x, 3.2f);

    /*
    auto group = wordContext.CreateGroup<Position>();
    group.each([&](Entity entity, Position& pos)
        {
            pos.x += 1.f;
        }
    );
    ASSERT_EQ(wordContext.Get<Position>(ent1).x, 4.f);
    ASSERT_EQ(wordContext.Get<Position>(ent3).x, 4.2f);
    */

    auto multiGroup = wordContext.CreateGroup<Position>(Include<Velocity>);
    multiGroup.each([&](Entity entity, Position& p, Velocity& v)
        {
            auto& pos = multiGroup.get<Position>(entity);
            auto& vel = multiGroup.get<Velocity>(entity);
            pos.x += vel.dx;
            pos.y += vel.dy;
        }
    );
    ASSERT_FLOAT_EQ(wordContext.Get<Position>(ent1).x, 3.f);
    ASSERT_FLOAT_EQ(wordContext.Get<Position>(ent3).x, 3.4f);
    ASSERT_FLOAT_EQ(wordContext.Get<Position>(ent3).y, 3.8f);
}
/*
    已废弃

TEST(ECSTest, HierarchySetUp)
{
    WorldContext wordContext;

    auto ent = wordContext.CreateEntityInHierarchy();
    EXPECT_EQ(ent, NullEntity);
    EXPECT_FALSE(wordContext.Valid(ent));
    EXPECT_EQ(wordContext.Size(), 0);
    auto parent = wordContext.CreateEntity();
    auto ent2 = wordContext.CreateEntityWithParent(parent);
    EXPECT_EQ(ent2, NullEntity);
    EXPECT_FALSE(wordContext.Valid(ent2));
    EXPECT_EQ(wordContext.Size(), 1);
    
    wordContext.Clear();
    wordContext.SetUpEntityHierarchy();
    auto ent3 = wordContext.CreateEntityInHierarchy();
    EXPECT_NE(ent3, NullEntity);
    EXPECT_EQ(wordContext.Size(), 1);
    EXPECT_EQ(wordContext.GetEntityHierarchy().Size(), 1);
}

TEST(ECSTest, HierarchyFunctionalities)
{
    WorldContext wordContext;
    wordContext.SetUpEntityHierarchy();

    Entity root = wordContext.CreateEntityInHierarchy("root");

    Entity ent1 = wordContext.CreateEntityWithParent("ent1", root);
    Entity ent2 = wordContext.CreateEntityWithParent("ent2", root);
    ASSERT_NE(ent1, NullEntity);
    ASSERT_NE(ent2, NullEntity);

    const auto& hierarchy = wordContext.GetEntityHierarchy();
    
    EXPECT_EQ(hierarchy.Size(), 3);
    EXPECT_TRUE(hierarchy.Contain(root));
    EXPECT_FALSE(hierarchy.Empty());

    EXPECT_EQ(hierarchy.GetRoots().size(), 1);
    EXPECT_EQ(hierarchy.GetRoots()[0], root);

    EXPECT_TRUE(hierarchy.IsAncestor(ent1, root));
    EXPECT_TRUE(hierarchy.IsAncestor(ent2, root));
    EXPECT_FALSE(hierarchy.IsAncestor(ent2, ent1));
    
    eastl::vector<Entity> children= hierarchy.GetChildren(root);
    EXPECT_EQ(children.size(), 2);
    EXPECT_EQ(children[0], ent1);
    EXPECT_EQ(children[1], ent2);

    Entity ent3 = wordContext.CreateEntityWithParent("ent3", ent1);
    Entity ent4 = wordContext.CreateEntityWithParent("ent4", ent1);
    EXPECT_TRUE(hierarchy.IsAncestor(ent3, root));

    eastl::vector<Entity> path = hierarchy.FindPath(ent3);
    EXPECT_EQ(path.size(), 2);
    EXPECT_EQ(path[0], root);
    EXPECT_EQ(path[1], ent1);

    wordContext.Clear();
    EXPECT_EQ(hierarchy.Size(), 0);
    EXPECT_FALSE(hierarchy.Contain(root));
    EXPECT_TRUE(hierarchy.Empty());
}

TEST(ECSTest, HierarchyDFS)
{
    WorldContext wordContext;
    wordContext.SetUpEntityHierarchy();

    Entity root = wordContext.CreateEntityInHierarchy("root");

    Entity ent1 = wordContext.CreateEntityWithParent("ent1", root);
    Entity ent2 = wordContext.CreateEntityWithParent("ent2", root);
    Entity ent3 = wordContext.CreateEntityWithParent("ent3", ent1);
    Entity ent4 = wordContext.CreateEntityWithParent("ent4", ent1);
    ASSERT_NE(ent1, NullEntity);
    ASSERT_NE(ent2, NullEntity);
    ASSERT_NE(ent3, NullEntity);
    ASSERT_NE(ent4, NullEntity);
    EXPECT_EQ(wordContext.Size(), 5);

    eastl::vector<Entity> dfsOrder = {root, ent1, ent3, ent4, ent2};
    int index = 0;
    const auto& hierarchy = wordContext.GetEntityHierarchy();
    hierarchy.DFSTraversal(root, 
        [&dfsOrder, &index](const Entity& ent)
        {
            EXPECT_EQ(ent, dfsOrder[index++]);
            return;
        }
    );

    wordContext.MoveEntityNode(ent1, ent2);
    eastl::vector<Entity> dfsOrder2 = {root, ent2, ent1, ent3, ent4};
    int index2 = 0;
    hierarchy.DFSTraversal(root, 
        [&dfsOrder2, &index2](const Entity& ent)
        {
            EXPECT_EQ(ent, dfsOrder2[index2++]);
            return;
        }
    );

    Entity ent5 = wordContext.CreateEntityWithParent("ent5", root);
    EXPECT_EQ(wordContext.Size(), 6);
    wordContext.DestoryEntityInHierarchy(ent1);
    EXPECT_EQ(wordContext.Size(), 3);
    EXPECT_EQ(hierarchy.Size(), 3);
    eastl::vector<Entity> dfsOrder3 = {root, ent2, ent5};
    int index3 = 0;
    hierarchy.DFSTraversal(root, 
        [&dfsOrder3, &index3](const Entity& ent)
        {
            EXPECT_EQ(ent, dfsOrder3[index3++]);
            return;
        }
    );

    wordContext.MoveEntityNode(ent5);
    EXPECT_EQ(wordContext.Size(), 3);
    EXPECT_EQ(hierarchy.Size(), 3);
    EXPECT_EQ(hierarchy.GetRoots().size(), 2);
    EXPECT_EQ(hierarchy.GetRoots()[0], root);
    EXPECT_EQ(hierarchy.GetRoots()[1], ent5);

    wordContext.DestoryEntityInHierarchy(root);
    EXPECT_FALSE(hierarchy.Empty());
    EXPECT_EQ(wordContext.Size(), 1);
    EXPECT_EQ(hierarchy.Size(), 1);
}
*/
TEST(ECSTest, Tag)
{
    WorldContext wordContext;
    Entity ent = wordContext.CreateEntity("test");
    Entity ent1 = wordContext.CreateEntity("test1");

    wordContext.Add<DeadTag>(ent);

    EXPECT_TRUE(wordContext.Has<DeadTag>(ent));

    auto view = wordContext.GetView<Name>(ExcludeDeadTag);
    for (auto ent: view)
    {
        EXPECT_EQ(wordContext.Get<Name>(ent).m_name, "test1");
        EXPECT_NE(wordContext.Get<Name>(ent).m_name, "test");
    }

    auto otherView = wordContext.GetView<Name, DeadTag>();
    for (auto& [ent, name]: otherView.each())
    {
        EXPECT_EQ(name.m_name, "test");
        EXPECT_NE(name.m_name, "test1");
    }

    auto OrderView = wordContext.GetView<DeadTag, Name>();
    for (auto& [ent, name]: OrderView.each())
    {
        EXPECT_EQ(name.m_name, "test");
        EXPECT_NE(name.m_name, "test1");
    }
}

class SampleSystemInferface
{
public:
    SampleSystemInferface() = default;
    virtual ~SampleSystemInferface() = default;

    virtual void Process(WorldContext& worldContext) = 0;
};

class SampleSystem final : public Service<SampleSystemInferface>::Handler
{
public:
    SampleSystem()
    {
        std::cout << "System Construct\n";
    }

    ~SampleSystem()
    {
        std::cout << "System Destruct\n";
    }

    void Process(WorldContext& worldContext)
    {
        auto view = worldContext.GetView<Position, Velocity>(ExcludeDeadTag);
        for(auto& [ent, pos, vel]: view.each())
        {
            pos.x += vel.dx;
            pos.y += vel.dy;
        }
    }
};

TEST(ECSTest, System)
{
    WorldContext worldContext;

    auto ent1 = worldContext.CreateEntity("ent1");
    worldContext.Add<Position>(ent1, 0.f, 0.f);
    worldContext.Add<Velocity>(ent1, 0.2f, 0.2f);

    auto ent2 = worldContext.CreateEntity("ent2");
    worldContext.Add<Position>(ent2, 10.f, 10.f);
    worldContext.Add<Velocity>(ent2, -0.5f, -0.2f);

    auto ent3 = worldContext.CreateEntity("ent3");
    worldContext.Add<Position>(ent3, 10.f, 10.f);
    worldContext.Add<Velocity>(ent3, -0.5f, -0.2f);
    worldContext.Add<DeadTag>(ent3);
    
    std::unique_ptr<SampleSystem> ptr = std::make_unique<SampleSystem>();

    auto sys = Service<SampleSystemInferface>::Get();
    ASSERT_NE(sys, nullptr);
    sys->Process(worldContext);
    
    EXPECT_FLOAT_EQ(worldContext.Get<Position>(ent1).x, 0.2f);
    EXPECT_FLOAT_EQ(worldContext.Get<Position>(ent1).y, 0.2f);
    EXPECT_FLOAT_EQ(worldContext.Get<Position>(ent2).x, 9.5f);
    EXPECT_FLOAT_EQ(worldContext.Get<Position>(ent2).y, 9.8f);
    EXPECT_FLOAT_EQ(worldContext.Get<Position>(ent3).x, 10.f);
    EXPECT_FLOAT_EQ(worldContext.Get<Position>(ent3).y, 10.f);

    ptr.reset();
    ASSERT_EQ(Service<SampleSystemInferface>::Get(), nullptr);
    
    // 同时创建两个对象，只有第一个创建的会被注册
    auto ptr2 = std::make_unique<SampleSystem>();
    auto ptr3 = std::make_unique<SampleSystem>();

    auto getPtr = Service<SampleSystemInferface>::Get();
    ASSERT_EQ(getPtr, ptr2.get());
    ASSERT_NE(getPtr, ptr3.get());

    ASSERT_FALSE(Service<SampleSystemInferface>::Unregister(ptr3.get()));

    ASSERT_TRUE(Service<SampleSystemInferface>::Unregister(ptr2.get()));
    ASSERT_TRUE(Service<SampleSystemInferface>::Register(ptr3.get()));
    getPtr = Service<SampleSystemInferface>::Get();
    ASSERT_EQ(getPtr, ptr3.get());
    ASSERT_NE(getPtr, ptr2.get());
}

template<typename D>
class ISelfSystem
{
public:
    ISelfSystem() = default;
    virtual ~ISelfSystem() = default;
    
    template<typename... Args>
    void Process(Args&&... args)
    {
        return static_cast<D*>(this)->ProcessImpl(eastl::forward<Args>(args)...);
    }
};

class SelfSystem final : public Service<ISelfSystem<SelfSystem>>::Handler
{
public:
    SelfSystem()
    {
        std::cout << "SelfSystem Construct\n";
    }

    template<typename... Args>
    void ProcessImpl(Args&&... args)
    {
        std::cout << "SelfSystem Process" << "\n";
    }

    ~SelfSystem()
    {
        std::cout << "SelfSystem Destruct\n";
    }
};

TEST(ECSTest, SelfSystem)
{
    auto sys = std::make_unique<SelfSystem>();

    ASSERT_NE(Service<ISelfSystem<SelfSystem>>::Get(), nullptr);
    Service<ISelfSystem<SelfSystem>>::Get()->Process();
}

class EntityHandler : public EntityEventBus::Handler
{
public:
    EntityHandler()
    {
        EntityEventBus::Handler::BusConnect();
    }

    ~EntityHandler()
    {
        EntityEventBus::Handler::BusDisconnect();
    }

    void OnEntityCreate(Entity entity) override
    {
        m_lastEntity = entity;
        m_enityCount++;
    }

    void OnEntityDestory(Entity entity) override
    {
        m_enityCount--;
    }

    uint32_t Count() const
    {
        return m_enityCount;
    }

    Entity Last() const
    {
        return m_lastEntity;
    }

private:
    uint32_t m_enityCount {0};
    Entity m_lastEntity {NullEntity};

};

TEST(ECSTest, EntityBus)
{
    WorldContext context;
    EntityHandler handler;

    Entity ent1 = context.CreateEntity();
    EXPECT_EQ(handler.Count(), 1);
    EXPECT_EQ(handler.Last(), ent1);

    Entity ent2 = context.CreateEntity();
    EXPECT_EQ(handler.Count(), 2);
    EXPECT_EQ(handler.Last(), ent2);

    context.DestoryEntity(ent1);
    EXPECT_EQ(handler.Count(), 1);
    context.DestoryEntity(ent2);
    EXPECT_EQ(handler.Count(), 0);
}

class ComponentHandler : ComponentEventBus::MultiHandler
{
public:
    ComponentHandler()
    {
        ComponentEventBus::MultiHandler::BusConnect(GetTypeId<Position>());
        ComponentEventBus::MultiHandler::BusConnect(GetTypeId<Velocity>());
    }

    ~ComponentHandler()
    {
        ComponentEventBus::MultiHandler::BusDisconnect();
    }

    void OnComponentConstruct(WorldContext& context, Entity entity) override
    {
        if (!context.Valid(entity))
        {
            return;
        }

        if (context.HasAll<Position>(entity))
        {
            m_position = context.Get<Position>(entity);
        }
        if (context.HasAll<Velocity>(entity))
        {
            m_velocity = context.Get<Velocity>(entity);
        }
    }

    void OnComponentUpdate(WorldContext& context, Entity entity) override
    {
        if (context.HasAll<Position>(entity))
        {
            m_position = context.Get<Position>(entity);
        }
        if (context.HasAll<Velocity>(entity))
        {
            m_velocity = context.Get<Velocity>(entity);
        }
    }

    void OnComponentDestory(WorldContext& context, Entity entity) override
    {
        if (context.HasAll<Position>(entity))
        {
            m_position.x = 0.f;
            m_position.y = 0.f;
        }
        if (context.HasAll<Velocity>(entity))
        {
            m_velocity.dx = 0.f;
            m_velocity.dy = 0.f;
        }
    }

public:
    Position m_position;
    Velocity m_velocity;
};

TEST(ECSTest, ComponentBus)
{
    WorldContext context;
    ComponentHandler handler;

    //context.SetupComponentEvents<Position>();
    //context.SetupComponentEvents<Velocity>();
    context.SetupComponentsEvents<Position, Velocity>();

    auto ent1 = context.CreateEntity();
    context.Add<Position>(ent1, 1.f, 1.f);
    EXPECT_FLOAT_EQ(handler.m_position.x, 1.f);
    EXPECT_FLOAT_EQ(handler.m_position.y, 1.f);

    context.Add<Velocity>(ent1, 0.2f, 0.2f);
    EXPECT_FLOAT_EQ(handler.m_velocity.dx, 0.2f);
    EXPECT_FLOAT_EQ(handler.m_velocity.dy, 0.2f);

    context.AddOrRepalce<Position>(ent1, 2.f, 2.f);
    context.Repalce<Velocity>(ent1, 0.5f, 0.5f);
    EXPECT_FLOAT_EQ(handler.m_position.x, 2.f);
    EXPECT_FLOAT_EQ(handler.m_position.y, 2.f);
    EXPECT_FLOAT_EQ(handler.m_velocity.dx, 0.5f);
    EXPECT_FLOAT_EQ(handler.m_velocity.dy, 0.5f);

    context.Remove<Position, Velocity>(ent1);
    EXPECT_FLOAT_EQ(handler.m_position.x, 0.f);
    EXPECT_FLOAT_EQ(handler.m_position.y, 0.f);
    EXPECT_FLOAT_EQ(handler.m_velocity.dx, 0.f);
    EXPECT_FLOAT_EQ(handler.m_velocity.dy, 0.f);
}

TEST(ECSTest, DestoryEntityAndComponentBus)
{
    WorldContext context;
    ComponentHandler handler;

    context.SetupComponentsEvents<Position>();

    auto ent1 = context.CreateEntity();
    context.Add<Position>(ent1, 1.f, 1.f);
    EXPECT_FLOAT_EQ(handler.m_position.x, 1.f);
    EXPECT_FLOAT_EQ(handler.m_position.y, 1.f);

    context.DestoryEntity(ent1);
    EXPECT_FLOAT_EQ(handler.m_position.x, 0.f);
    EXPECT_FLOAT_EQ(handler.m_position.y, 0.f);
}
