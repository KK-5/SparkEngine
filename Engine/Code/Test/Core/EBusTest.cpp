#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <EASTL/unique_ptr.h>
#include <EASTL/string_view.h>
#include <EASTL/fixed_vector.h>
#include <mutex>
#include <thread>
#include <random>

#include <EBus/EBus.h>
#include <EBus/Result.h>
#include <Log/SpdLogSystem.h>


using namespace Spark;
using ::testing::InSequence;

class TestInterface: public EBusTraits
{
public:
    static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
    static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::Single;

public:
    virtual void OnEvent() = 0;
    virtual void Log(eastl::string_view) = 0;
};

using TestBus = EBus<TestInterface>;

class TestBusHandler: public TestBus::Handler
{
public:
    void OnEvent() override
    {
        m_callEvents++;
    }

    void Log(eastl::string_view message) override
    {
        LOG_INFO("[TestBusHandler] {}", message);
    }

public:
   uint32_t m_callEvents {0};
};

TEST(EBusTest, ConnectDisconnect)
{
    EXPECT_FALSE(TestBus::HasHandlers());
    TestBusHandler handler;
    handler.BusConnect();

    EXPECT_TRUE(handler.BusIsConnected());
    EXPECT_TRUE(TestBus::HasHandlers());
    EXPECT_EQ(TestBus::GetTotalNumOfEventHandlers(), 1);

    TestBus::Broadcast(&TestBus::Events::OnEvent);
    EXPECT_EQ(handler.m_callEvents, 1);
    TestBus::Broadcast(&TestBus::Events::OnEvent);
    EXPECT_EQ(handler.m_callEvents, 2);

    TestBus::Broadcast(&TestBus::Events::Log, "Hello handler");
    handler.BusDisconnect();
    EXPECT_FALSE(handler.BusIsConnected());
    EXPECT_FALSE(TestBus::HasHandlers());
    EXPECT_EQ(TestBus::GetTotalNumOfEventHandlers(), 0);
}

class Mul2MulTraits: public EBusTraits
{
public:
    static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
    static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ById;

    using BusIdType = uint32_t;
};

class TestIdInterface: public EBusTraits
{
public:
    virtual void OnEvent() = 0;
};

using TestIdBus = EBus<TestIdInterface, Mul2MulTraits>;

class TestIdBusHandler: public TestIdBus::Handler
{
    void OnEvent() override
    {
        m_callEvents++;
    }
public:
   uint32_t m_callEvents {0};
};


TEST(EBusTest, IdTest)
{
    TestIdBusHandler handler1, handler2;
    handler1.BusConnect(1);
    handler2.BusConnect(2);
    
    EXPECT_TRUE(TestIdBus::HasHandlers());
    EXPECT_TRUE(TestIdBus::HasHandlers(1));
    EXPECT_TRUE(TestIdBus::HasHandlers(2));
    EXPECT_FALSE(TestIdBus::HasHandlers(3));

    TestIdBus::Broadcast(&TestIdBus::Events::OnEvent);
    EXPECT_EQ(handler1.m_callEvents, 1);
    EXPECT_EQ(handler2.m_callEvents, 1);

    TestIdBus::Event(1, &TestIdBus::Events::OnEvent);
    EXPECT_EQ(handler1.m_callEvents, 2);
    EXPECT_EQ(handler2.m_callEvents, 1);

    TestIdBus::Event(2, &TestIdBus::Events::OnEvent);
    EXPECT_EQ(handler1.m_callEvents, 2);
    EXPECT_EQ(handler2.m_callEvents, 2);
    
    handler1.BusDisconnect(1);
    handler2.BusDisconnect(2);
    EXPECT_FALSE(TestIdBus::HasHandlers());
}

class HandlerOrderTraits: public EBusTraits
{
public:
    static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::MultipleAndOrdered;
    static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ById;

    using BusIdType = uint32_t;
};

class OrderedInterface
{
public:
    virtual void OnEvent() = 0;
    virtual bool Compare(const OrderedInterface* other) const = 0;
    virtual uint32_t GetOrder() const = 0;
    virtual void OrderEvent() = 0;
};

using OrderedHandlerBus = EBus<OrderedInterface, HandlerOrderTraits>;

class OrderedHandler: public OrderedHandlerBus::Handler
{
public:
    MOCK_METHOD(void, OrderEvent, (), (override));

    void OnEvent() override
    {
        m_callEvents++;
    }

    uint32_t GetOrder() const override
    {
        return m_order;
    }

    bool Compare(const OrderedInterface* other) const override
    {
        return GetOrder() < other->GetOrder();
    }
public:
   uint32_t m_callEvents {0};
   uint32_t m_order {0};

};

TEST(EBusTest, OrderedHandlerTest)
{
    OrderedHandler handler1, handler2, handler3;
    handler1.m_order = 1;
    handler2.m_order = 2;
    handler3.m_order = 3;

    handler3.BusConnect(100);
    handler1.BusConnect(100);
    handler2.BusConnect(100);

    EXPECT_TRUE(OrderedHandlerBus::HasHandlers(100));
    EXPECT_EQ(OrderedHandlerBus::GetTotalNumOfEventHandlers(), 3);
    EXPECT_EQ(OrderedHandlerBus::GetNumOfEventHandlers(100), 3);

    EXPECT_EQ(&handler1, OrderedHandlerBus::FindFirstHandler(100));

    {
        InSequence eventSeq;
        EXPECT_CALL(handler1, OrderEvent());
        EXPECT_CALL(handler2, OrderEvent());
        EXPECT_CALL(handler3, OrderEvent());
        OrderedHandlerBus::Event(100, &OrderedHandlerBus::Events::OrderEvent);
    }
    {
        InSequence eventSeq;
        EXPECT_CALL(handler1, OrderEvent());
        EXPECT_CALL(handler2, OrderEvent());
        EXPECT_CALL(handler3, OrderEvent());
        OrderedHandlerBus::Broadcast(&OrderedHandlerBus::Events::OrderEvent);
    }

    OrderedHandlerBus::Event(100, &OrderedHandlerBus::Events::OnEvent);
    EXPECT_EQ(handler1.m_callEvents, 1);
    EXPECT_EQ(handler2.m_callEvents, 1);
    EXPECT_EQ(handler3.m_callEvents, 1);

    handler3.BusDisconnect(100);
    handler1.BusDisconnect(100);
    handler2.BusDisconnect(100);
}

class AddressOrderTraits: public EBusTraits
{
public:
    static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
    static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ByIdAndOrdered;

    using BusIdType = uint32_t;
    using BusIdOrderCompare = eastl::less<uint32_t>;
};

class AddressOrderInterface
{
public:
    virtual void OnEvent() = 0;
    virtual void OrderEvent() = 0;
};

using AddressOrderBus = EBus<AddressOrderInterface, AddressOrderTraits>;

class AddressOrderHandler: public AddressOrderBus::Handler
{
public:
    MOCK_METHOD(void, OrderEvent, (), (override));

    void OnEvent() override
    {
        m_callEvents++;
    }
public:
    uint32_t m_callEvents{0};
};

TEST(EBusTest, OrderedAddressTest)
{
    AddressOrderHandler handler1, handler2, handler3;
    handler1.BusConnect(5);
    handler2.BusConnect(10);
    handler3.BusConnect(15);

    EXPECT_EQ(AddressOrderBus::GetTotalNumOfEventHandlers(), 3);
    EXPECT_EQ(AddressOrderBus::GetNumOfEventHandlers(5), 1);
    EXPECT_EQ(AddressOrderBus::GetNumOfEventHandlers(10), 1);
    EXPECT_EQ(AddressOrderBus::GetNumOfEventHandlers(15), 1);

    {
        InSequence eventSeq;
        EXPECT_CALL(handler1, OrderEvent());
        EXPECT_CALL(handler2, OrderEvent());
        EXPECT_CALL(handler3, OrderEvent());
        AddressOrderBus::Broadcast(&AddressOrderBus::Events::OrderEvent);
    }

    AddressOrderBus::Event(5, &AddressOrderBus::Events::OnEvent);
    EXPECT_EQ(handler1.m_callEvents, 1);
    EXPECT_EQ(handler2.m_callEvents, 0);
    EXPECT_EQ(handler3.m_callEvents, 0);

    AddressOrderBus::Broadcast(&AddressOrderBus::Events::OnEvent);
    EXPECT_EQ(handler1.m_callEvents, 2);
    EXPECT_EQ(handler2.m_callEvents, 1);
    EXPECT_EQ(handler3.m_callEvents, 1);

    handler1.BusDisconnect(5);
    handler2.BusDisconnect(10);
    handler3.BusDisconnect(15);
}

class ConnectInferface
{
public:
    virtual void ConnectAllHandlers() = 0;
    virtual void DisconnectAllHandlers() = 0;
};

using ConnectInDispatchBus = EBus<ConnectInferface, EBusTraits>;

class ConnectHandler: public ConnectInDispatchBus::Handler
{
public:
    void ConnectAllHandlers() override
    {
        for(auto& handler: s_handlers)
        {
            handler->BusConnect();
        }
    }

    void DisconnectAllHandlers() override
    {
        for(auto& handler: s_handlers)
        {
            handler->BusDisconnect();
        }
    }

    void AddHandler(ConnectHandler* handler)
    {
        s_handlers.push_back(handler);
    }

    static eastl::fixed_vector<ConnectHandler*, 5> s_handlers;
};

eastl::fixed_vector<ConnectHandler*, 5> ConnectHandler::s_handlers;

TEST(EBusTest, ConnectInDispatch)
{
    ConnectHandler parentHandler;
    ConnectHandler handler1, handler2, handler3;
    parentHandler.AddHandler(&handler1);
    parentHandler.AddHandler(&handler2);
    parentHandler.AddHandler(&handler3);
    EXPECT_FALSE(ConnectInDispatchBus::HasHandlers());

    parentHandler.BusConnect();
    EXPECT_EQ(ConnectInDispatchBus::GetTotalNumOfEventHandlers(), 1);

    ConnectInDispatchBus::Broadcast(&ConnectInDispatchBus::Events::ConnectAllHandlers);
    EXPECT_EQ(ConnectInDispatchBus::GetTotalNumOfEventHandlers(), 4);

    ConnectInDispatchBus::Broadcast(&ConnectInDispatchBus::Events::DisconnectAllHandlers);
    EXPECT_EQ(ConnectInDispatchBus::GetTotalNumOfEventHandlers(), 1);

    parentHandler.BusDisconnect();
    EXPECT_FALSE(ConnectInDispatchBus::HasHandlers());
}

class QueueEventTraits: public EBusTraits
{
public:
    static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
    static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::Single;

    static constexpr bool EnableEventQueue = true;
};

class QueueEventInterface
{
public:
    virtual void OnEvent() = 0;
};

using QueueEventBus = EBus<QueueEventInterface, QueueEventTraits>;

class QueueEventHandler: public QueueEventBus::Handler
{
public:
    void OnEvent() override
    {
        m_callEvents++;
    }
public:
    uint32_t m_callEvents{0};
};

TEST(EBusTest, QueueEventTest)
{
    QueueEventHandler h1, h2;
    h1.BusConnect();
    h2.BusConnect();

    QueueEventBus::QueueBroadcast(&QueueEventBus::Events::OnEvent);
    EXPECT_EQ(h1.m_callEvents, 0);
    EXPECT_EQ(h2.m_callEvents, 0);

    QueueEventBus::AllowFunctionQueuing(false);
    QueueEventBus::ExecuteQueuedEvents();
    EXPECT_EQ(h1.m_callEvents, 0);
    EXPECT_EQ(h2.m_callEvents, 0);
    
    QueueEventBus::AllowFunctionQueuing(true);
    QueueEventBus::QueueBroadcast(&QueueEventBus::Events::OnEvent);
    EXPECT_EQ(h1.m_callEvents, 0);
    EXPECT_EQ(h2.m_callEvents, 0);
    QueueEventBus::ExecuteQueuedEvents();
    EXPECT_EQ(h1.m_callEvents, 1);
    EXPECT_EQ(h2.m_callEvents, 1);

    h1.BusDisconnect();
    h2.BusDisconnect();
}

struct MultiThreadTraits: public EBusTraits
{
    static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
    static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::Single;

    using MutexType = std::mutex;
    static constexpr bool LocklessDispatch = false;
};

class MultiThreadInterface
{
public:
    virtual void OnMultiThreadEvent() = 0;
};

using MutiThreadBus = EBus<MultiThreadInterface, MultiThreadTraits>;

class MultiThreadHandler final: public MutiThreadBus::Handler
{
public:
    MultiThreadHandler()
    {
        BusConnect();
    }

    ~MultiThreadHandler()
    {
        BusDisconnect();
    }

    void OnMultiThreadEvent() override
    {
        m_callEvents++;
        _sleep(2);
    }
public:
    static uint32_t m_callEvents;
};

uint32_t MultiThreadHandler::m_callEvents {0};

TEST(EBusTest, MutiThreadTest)
{
    MultiThreadHandler h1, h2;

    static constexpr int threadCount = 8;
    
    eastl::fixed_vector<std::thread, threadCount> threads;
    int loop = 3;
    uint32_t totalCallEvent = threadCount * 2 * loop;
    auto Work = [&]()
    {
        for(size_t i = 0; i < loop; ++i)
        {
            MutiThreadBus::Broadcast(&MutiThreadBus::Events::OnMultiThreadEvent);
            //LOG_INFO("[MutiThreadTest] OnMultiThreadEvent m_callEvents {}", MultiThreadHandler::m_callEvents);
        }
    };

    for(size_t i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(Work);
    }

    for(auto& thread: threads)
    {
        thread.join();
    }
    EXPECT_EQ(totalCallEvent, MultiThreadHandler::m_callEvents);
}

struct EBusResultTraits: public EBusTraits
{
    static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
    static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::Single;
};

struct EBusResultInterface
{
    virtual uint32_t Sum() = 0;
};

using EBusResultBus = EBus<EBusResultInterface, EBusResultTraits>;

struct EBusResultHandler final: public EBusResultBus::Handler
{
    EBusResultHandler(uint32_t value): m_value(value)
    {
        BusConnect();
    }

    ~EBusResultHandler()
    {
        BusDisconnect();
    }

    uint32_t Sum() override
    {
        return m_value;
    }

private:
    uint32_t m_value;
};

TEST(EBusTest, EBusResultRTest)
{
    static constexpr int handlerCount = 8;
    eastl::fixed_vector<EBusResultHandler, handlerCount> handlers;

    uint32_t actulSum = 0;
    for(size_t i = 0; i < handlerCount; ++i)
    {
        uint32_t value = rand() % 1000;
        LOG_INFO("[EBusResultRTest] value {}", value);
        actulSum += value;
        handlers.emplace_back(value);
    }
    
    uint32_t calculSum = 0;
    EBusReduceResult<uint32_t&, eastl::plus<uint32_t>> result(calculSum);
    EBusResultBus::BroadcastResult(result, &EBusResultBus::Events::Sum);
    EXPECT_EQ(calculSum, actulSum);

    EBusAggregateResults<uint32_t> results;
    EBusResultBus::BroadcastResult(results, &EBusResultBus::Events::Sum);
    uint32_t sum2 = 0;
    for(auto value: results.values)
    {
        LOG_INFO("[EBusResultRTest] results value {}", value);
        sum2 += value;
    }
    EXPECT_EQ(sum2, actulSum);
}