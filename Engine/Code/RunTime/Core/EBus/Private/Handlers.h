#pragma once

#include <EASTL/intrusive_ptr.h>
#include <EASTL/type_traits.h>
#include <EASTL/unordered_map.h>

#include <Log/SpdLogSystem.h>

#include "Polices.h"
#include "StoragePolices.h"

namespace Spark
{
    template <typename Interface, typename Traits>
    class EBus;

    struct NullMutex;
    
    /*
     * 对 Interface* 的封装，HandlerPolicy决定了它使用哪一种HandlerStorageNode
     * HandlerNode将被放到EBus容器之中
     */
    template <typename Interface, typename Traits, typename HandlerHolder, bool hasId = Traits::AddressPolicy != EBusAddressPolicy::Single>
    class HandlerNode
        : public HandlerStorageNode<HandlerNode<Interface, Traits, HandlerHolder, true>, Traits::HandlerPolicy>
    {
    public:
        HandlerNode(Interface* inst)
            : m_interface(inst)
        {
        }

        HandlerNode(const HandlerNode& rhs)
        {
            *this = rhs;
        }
        HandlerNode(HandlerNode&& rhs)
        {
            *this = eastl::move(rhs);
        }
        HandlerNode& operator=(const HandlerNode& rhs)
        {
            m_interface = rhs.m_interface;
            m_holder = rhs.m_holder;
            return *this;
        }
        HandlerNode& operator=(HandlerNode&& rhs)
        {
            m_interface = eastl::move(rhs.m_interface);
            m_holder = eastl::move(rhs.m_holder);
            return *this;
        }

        typename Traits::BusIdType GetBusId() const
        {
            return m_holder->m_busId;
        }

        operator Interface*() const
        {
            return m_interface;
        }

        Interface* operator->() const
        {
            return m_interface;
        }

        HandlerNode& operator=(Interface* inst)
        {
            m_interface = inst;
            if (inst == nullptr)
            {
                m_holder.reset();
            }
            return *this;
        }

        Interface* m_interface = nullptr;
        // 持有HandlerHolder的反向引用
        eastl::intrusive_ptr<HandlerHolder> m_holder;
    };
    
    // 如果 AddressPolicy == EBusAddressPolicy::Single，不需要HandlerHolder
    template <typename Interface, typename Traits, typename HandlerHolder>
    class HandlerNode<Interface, Traits, HandlerHolder, false>
        : public HandlerStorageNode<HandlerNode<Interface, Traits, HandlerHolder, false>, Traits::HandlerPolicy>
    {
    public:
        HandlerNode(Interface* inst)
            : m_interface(inst)
        {
        }

        HandlerNode(const HandlerNode& rhs)
        {
            *this = rhs;
        }
        HandlerNode(HandlerNode&& rhs)
        {
            *this = eastl::move(rhs);
        }
        HandlerNode& operator=(const HandlerNode& rhs)
        {
            m_interface = rhs.m_interface;
            return *this;
        }
        HandlerNode& operator=(HandlerNode&& rhs)
        {
            m_interface = eastl::move(rhs.m_interface);
            return *this;
        }

        operator Interface*() const
        {
            return m_interface;
        }
        Interface* operator->() const
        {
            return m_interface;
        }

        HandlerNode& operator=(Interface* inst)
        {
            m_interface = inst;
            return *this;
        }

        Interface* m_interface = nullptr;
    };

    
    // 所有类型的Handler都会继承Interface，Handler持有一个HandlerNode，this指针可以赋值给HandlerNode
    // 这样就可以让此对象的副本保存在HandlerNode中，进而保存在HandlerStoragePolicy容器中
    template <typename Interface, typename Traits, typename ContainerType>
    class NonIdHandler
        : public Interface
    {
    public:
        using BusType = EBus<Interface, Traits>;

        NonIdHandler()
            : m_node(nullptr)
        {
        }
        
        // 注意这里的拷贝和赋值操作行为不一样
        NonIdHandler(const NonIdHandler& rhs)
            : m_node(nullptr)
        {
            *this = rhs;
        }

        NonIdHandler& operator=(const NonIdHandler& rhs)
        {
            BusDisconnect();
            if (rhs.BusIsConnected())
            {
                BusConnect();
            }
            return *this;
        }

        NonIdHandler(NonIdHandler&& rhs)
            : m_node(nullptr)
        {
            *this = eastl::move(rhs);
        }

        NonIdHandler& operator=(NonIdHandler&& rhs)
        {
            BusDisconnect();
            if (rhs.BusIsConnected())
            {
                rhs.BusDisconnect();
                BusConnect();
            }
            return *this;
        }

        virtual ~NonIdHandler()
        {
            assert((!eastl::is_polymorphic<typename BusType::InterfaceType>::value ||
                    eastl::is_same<typename BusType::MutexType, NullMutex>::value ||
                    !BusIsConnected()) &&
                "EBus handlers must be disconnected prior to destruction on multi-threaded buses with virtual functions"
            );

            if (BusIsConnected())
            {
                BusDisconnect();
            }
            assert(!BusIsConnected() && "Internal error: Bus was not properly disconnected!");
        }
        
        // Connect时将this指针赋值给m_node，即Interface对象赋值给HandlerNode
        inline void BusConnect()
        {
            typename BusType::Context& context = BusType::GetOrCreateContext();
            typename BusType::Context::ConnectLockGuard contextLock(context.m_contextMutex);
            if (!BusIsConnected())
            {
                typename Traits::BusIdType id{};
                m_node = this;
                BusType::ConnectInternal(context, m_node, contextLock, id);
            }
        }

        inline void BusDisconnect()
        {
            if (typename BusType::Context* context = BusType::GetContext())
            {
                typename BusType::Context::ConnectLockGuard contextLock(context->m_contextMutex);
                if (BusIsConnected())
                {
                    BusType::DisconnectInternal(*context, m_node);
                }
            }
        }

        inline bool BusIsConnected() const
        {
            return static_cast<Interface*>(m_node) != nullptr;
        }

    private:
        // Must be a member and not a base type so that Interface may be an incomplete type.
        typename ContainerType::HandlerNode m_node;
    };

    template <typename Interface, typename Traits, typename ContainerType>
    class IdHandler
        : public Interface
    {
    private:
        using IdType = typename Traits::BusIdType;

    public:
        using BusType = EBus<Interface, Traits>;

        IdHandler()
            : m_node(nullptr)
        {
        }

        IdHandler(const IdHandler& rhs)
            : m_node(nullptr)
        {
            *this = rhs;
        }

        IdHandler& operator=(const IdHandler& rhs)
        {
            BusDisconnect();
            if (rhs.BusIsConnected())
            {
                BusConnect(rhs.m_node.GetBusId());
            }
            return *this;
        }

        IdHandler(IdHandler&& rhs)
            : m_node(nullptr)
        {
            *this = eastl::move(rhs);
        }

        IdHandler& operator=(IdHandler&& rhs)
        {
            BusDisconnect();
            if (rhs.BusIsConnected())
            {
                IdType id = rhs.m_node.GetBusId();
                rhs.BusDisconnect(id);
                BusConnect(id);
            }
            return *this;
        }

        virtual ~IdHandler()
        {
            assert((!eastl::is_polymorphic<typename BusType::InterfaceType>::value ||
                    eastl::is_same<typename BusType::MutexType, NullMutex>::value ||
                    !BusIsConnected()) &&
                "EBus handlers must be disconnected prior to destruction on multi-threaded buses with virtual functions"
            );

            if (BusIsConnected())
            {
                BusDisconnect();
            }
            assert(!BusIsConnected() && "Internal error: Bus was not properly disconnected!");
        }

        inline void BusConnect(const IdType& id)
        {
            typename BusType::Context& context = BusType::GetOrCreateContext();
            typename BusType::Context::ConnectLockGuard contextLock(context.m_contextMutex);
            if (BusIsConnected())
            {
                if (m_node.GetBusId() == id)
                {
                    return;
                }
                assert(false &&
                        "Connecting to a different id on this bus without disconnecting first! Please ensure you call BusDisconnect before "
                        "calling BusConnect again, or if multiple connections are desired you must use a MultiHandler instead.");
                BusType::DisconnectInternal(context, m_node);
            }

            m_node = this;
            BusType::ConnectInternal(context, m_node, contextLock, id);
        }

        inline void BusDisconnect(const IdType& id)
        {
            if (typename BusType::Context* context = BusType::GetContext())
            {
                typename BusType::Context::ConnectLockGuard contextLock(context->m_contextMutex);
                if (BusIsConnectedId(id))
                {
                    BusType::DisconnectInternal(*context, m_node);
                }
                else
                {
                    assert(false && "[EBus] The Handler is not connected to this Address on the EBus.");
                }
            }
        }

        inline void BusDisconnect()
        {
            if (typename BusType::Context* context = BusType::GetContext())
            {
                typename BusType::Context::ConnectLockGuard contextLock(context->m_contextMutex);
                if (BusIsConnected())
                {
                    BusType::DisconnectInternal(*context, m_node);
                }
            }
        }

        inline bool BusIsConnectedId(const IdType& id) const
        {
            return BusIsConnected() && m_node.GetBusId() == id;
        }

        inline bool BusIsConnected() const
        {
            return m_node.m_holder != nullptr;
        }

    private:
        // Must be a member and not a base type so that Interface may be an incomplete type.
        typename ContainerType::HandlerNode m_node;
    };

    // MultiHandler可以连接到EBus的多个Id上，所以需要使用一个Map保存这个Handler连接到了哪些Id
    template <typename Interface, typename Traits, typename ContainerType>
    class MultiHandler
        : public Interface
    {
    private:
        using IdType = typename Traits::BusIdType;
        using HandlerNode = typename ContainerType::HandlerNode;

    public:
        using BusType = EBus<Interface, Traits>;

        MultiHandler() = default;

        MultiHandler(const MultiHandler& rhs)
        {
            *this = rhs;
        }

        MultiHandler& operator=(const MultiHandler& rhs)
        {
            BusDisconnect();
            for (const auto& nodePair : rhs.m_handlerNodes)
            {
                BusConnect(nodePair.first);
            }
            return *this;
        }

        MultiHandler(MultiHandler&& rhs)
        {
            *this = eastl::move(rhs);
        }

        MultiHandler& operator=(MultiHandler&& rhs)
        {
            BusDisconnect();
            for (const auto& nodePair : rhs.m_handlerNodes)
            {
                BusConnect(nodePair.first);
            }
            rhs.BusDisconnect();
            return *this;
        }

        virtual ~MultiHandler()
        {
            assert((!eastl::is_polymorphic<typename BusType::InterfaceType>::value ||
                    eastl::is_same<typename BusType::MutexType, NullMutex>::value ||
                    !BusIsConnected()) &&
                "EBus handlers must be disconnected prior to destruction on multi-threaded buses with virtual functions"
            );

            if (BusIsConnected())
            {
                BusDisconnect();
            }
            assert(!BusIsConnected() && "Internal error: Bus was not properly disconnected!");
        }

        inline void BusConnect(const IdType& id)
        {
            typename BusType::Context& context = BusType::GetOrCreateContext();
            typename BusType::Context::ConnectLockGuard contextLock(context.m_contextMutex);
            if (m_handlerNodes.find(id) == m_handlerNodes.end())
            {
                void* handlerNodeAddr =
                    m_handlerNodes.get_allocator().allocate(sizeof(HandlerNode), eastl::alignment_of<HandlerNode>::value, /* offset = */0);
                auto handlerNode = new (handlerNodeAddr) HandlerNode(this);
                m_handlerNodes.emplace(id, eastl::move(handlerNode));
                BusType::ConnectInternal(context, *handlerNode, contextLock, id);
            }
        }
        inline void BusDisconnect(const IdType& id)
        {
            if (typename BusType::Context* context = BusType::GetContext())
            {
                typename BusType::Context::ConnectLockGuard contextLock(context->m_contextMutex);
                auto nodeIt = m_handlerNodes.find(id);
                if (nodeIt != m_handlerNodes.end())
                {
                    HandlerNode* handlerNode = nodeIt->second;
                    BusType::DisconnectInternal(*context, *handlerNode);
                    m_handlerNodes.erase(nodeIt);
                    handlerNode->~HandlerNode();
                    m_handlerNodes.get_allocator().deallocate(handlerNode, sizeof(HandlerNode));
                }
            }
        }

        inline void BusDisconnect()
        {
            decltype(m_handlerNodes) handlerNodesToDisconnect;
            if (typename BusType::Context* context = BusType::GetContext())
            {
                typename BusType::Context::ConnectLockGuard contextLock(context->m_contextMutex);
                handlerNodesToDisconnect = eastl::move(m_handlerNodes);

                for (const auto& nodePair : handlerNodesToDisconnect)
                {
                    BusType::DisconnectInternal(*context, *nodePair.second);

                    nodePair.second->~HandlerNode();
                    handlerNodesToDisconnect.get_allocator().deallocate(
                        nodePair.second, sizeof(HandlerNode));
                }
            }
        }

        inline bool BusIsConnectedId(const IdType& id) const
        {
            return m_handlerNodes.end() != m_handlerNodes.find(id);
        }

        inline bool BusIsConnected() const
        {
            return !m_handlerNodes.empty();
        }

    private:
        eastl::unordered_map<IdType, HandlerNode*, eastl::hash<IdType>, eastl::equal_to<IdType>, typename Traits::AllocatorType> m_handlerNodes;
    };

}