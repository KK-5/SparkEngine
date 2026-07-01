#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <gtest/gtest.h>

#include <ECS/Entity.h>
#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <ECS/Tag.h>
#include <ECS/ISystem.h>
#include <ECS/ComponentTraits.h>
#include <Service/Service.h>
#include <Log/ILogSystem.h>
#include <CoreComponents/Name.h>

#include <iostream>

using namespace Spark;

TEST(ECSTest, CreateDestoryEntity)
{
    WorldContext wordContext;

    Entity ent = wordContext.CreateEntity();
    ASSERT_TRUE(wordContext.Valid(ent));

    Entity namedEntity = wordContext.CreateEntity("MyName");
    ASSERT_TRUE(wordContext.Valid(namedEntity));
    ASSERT_EQ(wordContext.Get<Name>(namedEntity).name, "MyName");

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

namespace Spark
{
    template<>
    struct ComponentTraits<Position> : ComponentTraitsBase<Position>
    {
        static constexpr ComponentEventMask componentEvents = ComponentEventMask::All;
    };

    template<>
    struct ComponentTraits<Velocity> : ComponentTraitsBase<Velocity>
    {
        static constexpr ComponentEventMask componentEvents = ComponentEventMask::All;
    };
}

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
        EXPECT_EQ(wordContext.Get<Name>(ent).name, "test1");
        EXPECT_NE(wordContext.Get<Name>(ent).name, "test");
    }

    auto otherView = wordContext.GetView<Name, DeadTag>();
    for (auto& [ent, name]: otherView.each())
    {
        EXPECT_EQ(name.name, "test");
        EXPECT_NE(name.name, "test1");
    }

    auto OrderView = wordContext.GetView<DeadTag, Name>();
    for (auto& [ent, name]: OrderView.each())
    {
        EXPECT_EQ(name.name, "test");
        EXPECT_NE(name.name, "test1");
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
        LOG_INFO("System Construct");
    }

    ~SampleSystem()
    {
        LOG_INFO("System Destruct");
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
        LOG_INFO("SelfSystem Construct");
    }

    template<typename... Args>
    void ProcessImpl(Args&&... args)
    {
        LOG_INFO("SelfSystem Process");
    }

    ~SelfSystem()
    {
        LOG_INFO("SelfSystem Destruct");
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

    WorldExecuteContextGuard guard(context);

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

    void OnComponentConstruct(Entity entity) override
    {
        ASSERT_NE(WorldExecuteContext::Current(), nullptr);
        auto& context = *WorldExecuteContext::Current();
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

    void OnComponentUpdated(Entity entity) override
    {
        ASSERT_NE(WorldExecuteContext::Current(), nullptr);
        auto& context = *WorldExecuteContext::Current();
        if (context.HasAll<Position>(entity))
        {
            m_position = context.Get<Position>(entity);
        }
        if (context.HasAll<Velocity>(entity))
        {
            m_velocity = context.Get<Velocity>(entity);
        }
    }

    void OnComponentDestory(Entity entity) override
    {
        ASSERT_NE(WorldExecuteContext::Current(), nullptr);
        auto& context = *WorldExecuteContext::Current();
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

    WorldExecuteContextGuard guard(context);

    auto ent1 = context.CreateEntity();
    context.Add<Position>(ent1, 1.f, 1.f);
    EXPECT_FLOAT_EQ(handler.m_position.x, 1.f);
    EXPECT_FLOAT_EQ(handler.m_position.y, 1.f);

    context.Add<Velocity>(ent1, 0.2f, 0.2f);
    EXPECT_FLOAT_EQ(handler.m_velocity.dx, 0.2f);
    EXPECT_FLOAT_EQ(handler.m_velocity.dy, 0.2f);

    context.AddOrReplace<Position>(ent1, 2.f, 2.f);
    context.Replace<Velocity>(ent1, 0.5f, 0.5f);
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

TEST(ECSTest, DestoryEntityWithoutRegisterEventOnEntityRemove)
{
    WorldContext context;
    ComponentHandler handler;

    WorldExecuteContextGuard guard(context);

    auto ent1 = context.CreateEntity();
    context.Add<Position>(ent1, 1.f, 1.f);
    EXPECT_FLOAT_EQ(handler.m_position.x, 1.f);
    EXPECT_FLOAT_EQ(handler.m_position.y, 1.f);

    context.DestoryEntity(ent1);
    EXPECT_FLOAT_EQ(handler.m_position.x, 1.f);
    EXPECT_FLOAT_EQ(handler.m_position.y, 1.f);
}

TEST(ECSTest, DestoryEntityAndComponentBus)
{
    WorldContext context;
    ComponentHandler handler;
    context.RegisterEventOnEntityRemove<Position>();

    WorldExecuteContextGuard guard(context);

    auto ent1 = context.CreateEntity();
    context.Add<Position>(ent1, 1.f, 1.f);
    EXPECT_FLOAT_EQ(handler.m_position.x, 1.f);
    EXPECT_FLOAT_EQ(handler.m_position.y, 1.f);

    context.DestoryEntity(ent1);
    EXPECT_FLOAT_EQ(handler.m_position.x, 0.f);
    EXPECT_FLOAT_EQ(handler.m_position.y, 0.f);
}

TEST(ECSTest, DestoryEntityWithRegisterEventsOnEntityRemove)
{
    WorldContext context;
    ComponentHandler handler;
    context.RegisterEventsOnEntityRemove<Position, Velocity>();

    WorldExecuteContextGuard guard(context);

    auto ent1 = context.CreateEntity();
    context.Add<Position>(ent1, 1.f, 1.f);
    context.Add<Velocity>(ent1, 0.2f, 0.3f);
    EXPECT_FLOAT_EQ(handler.m_position.x, 1.f);
    EXPECT_FLOAT_EQ(handler.m_position.y, 1.f);
    EXPECT_FLOAT_EQ(handler.m_velocity.dx, 0.2f);
    EXPECT_FLOAT_EQ(handler.m_velocity.dy, 0.3f);

    context.DestoryEntity(ent1);
    EXPECT_FLOAT_EQ(handler.m_position.x, 0.f);
    EXPECT_FLOAT_EQ(handler.m_position.y, 0.f);
    EXPECT_FLOAT_EQ(handler.m_velocity.dx, 0.f);
    EXPECT_FLOAT_EQ(handler.m_velocity.dy, 0.f);
}
