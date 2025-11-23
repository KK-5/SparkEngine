#pragma once
#include <EASTL/functional.h>
#include <EASTL/intrusive_ptr.h>
#include <EASTL/atomic.h>

#include "StoragePolices.h"
#include "Handlers.h"

namespace Spark
{
    // Default impl, used when there are multiple addresses and multiple handlers
    template <typename Interface, typename Traits, EBusAddressPolicy addressPolicy = Traits::AddressPolicy, EBusHandlerPolicy handlerPolicy = Traits::HandlerPolicy>
    struct EBusContainer
    {
    public:
        using ContainerType = EBusContainer;
        using IdType = typename Traits::BusIdType;

        // This struct will hold the handlers per address
        struct HandlerHolder;
        // This struct will hold each handler
        using HandlerNode = HandlerNode<Interface, Traits, HandlerHolder>;
        // Defines how handler holders are stored (will be some sort of map-like structure from id -> handler holder)
        using AddressStorage = AddressStoragePolicy<Traits, HandlerHolder>;
        // Defines how handlers are stored per address (will be some sort of list)
        using HandlerStorage = HandlerStoragePolicy<Interface, Traits, HandlerNode>;

        using Handler = IdHandler<Interface, Traits, ContainerType>;
        using MultiHandler = MultiHandler<Interface, Traits, ContainerType>;
        using BusPtr = eastl::intrusive_ptr<HandlerHolder>;

        struct HandlerHolder
        {
            ContainerType& m_busContainer;
            IdType m_busId;
            typename HandlerStorage::StorageType m_handlers;
            eastl::atomic<unsigned int> m_refCount{ 0 };

            HandlerHolder(ContainerType& storage, const IdType& id)
                : m_busContainer(storage)
                , m_busId(id)
            { }

            HandlerHolder(HandlerHolder&& rhs)
                : m_busContainer(rhs.m_busContainer)
                , m_busId(rhs.m_busId)
                , m_handlers(eastl::move(rhs.m_handlers))
            {
                m_refCount.store(rhs.m_refCount.load());
                rhs.m_refCount.store(0);
            }

            HandlerHolder(const HandlerHolder&) = delete;
            HandlerHolder& operator=(const HandlerHolder&) = delete;
            HandlerHolder& operator=(HandlerHolder&&) = delete;

            ~HandlerHolder() = default;

            // Returns true if this HandlerHolder actually has handers.
            // This will return false when this id is Bind'ed to, but there are no handlers.
            bool HasHandlers() const
            {
                return !m_handlers.empty();
            }

            void AddRef()
            {
                m_refCount.fetch_add(1);
            }
            void Release()
            {
                // Must check against 1 because fetch_sub returns the value before decrementing
                if (m_refCount.fetch_sub(1) == 1)
                {
                    m_busContainer.m_addresses.erase(m_busId);
                }
            }
        };

        void Bind(BusPtr& busPtr, const IdType& id)
        {
            busPtr = &FindOrCreateHandlerHolder(id);
        }

        HandlerHolder& FindOrCreateHandlerHolder(const IdType& id)
        {
            auto addressIt = m_addresses.find(id);
            if (addressIt == m_addresses.end())
            {
                addressIt = m_addresses.emplace(eastl::pair<IdType, HandlerHolder>(
                    id, 
                    HandlerHolder(*this, id))
                );
            }

            return addressIt->second;
        }

        void Connect(HandlerNode& handler, const IdType& id)
        {
            assert(!handler.m_holder &&
                "Internal error: Connect() may not be called by a handler that is already connected."
                "BusConnect() should already have handled this case.");

            HandlerHolder& holder = FindOrCreateHandlerHolder(id);
            holder.m_handlers.insert(handler);
            handler.m_holder = &holder;
        }

        void Disconnect(HandlerNode& handler)
        {
            assert(handler.m_holder && "Internal error: disconnecting handler that is incompletely connected");

            handler.m_holder->m_handlers.erase(handler);

            // Must reset handler after removing it from the list, otherwise m_holder could have been destroyed already (and handlerList would be invalid)
            handler.m_holder.reset();
        }
        
        // set of HandlerHolder
        typename AddressStorage::StorageType m_addresses;
    };

    // Specialization for multi address, single handler
    template <typename Interface, typename Traits, EBusAddressPolicy addressPolicy>
    struct EBusContainer<Interface, Traits, addressPolicy, EBusHandlerPolicy::Single>
    {
    public:
        using ContainerType = EBusContainer;
        using IdType = typename Traits::BusIdType;

        // This struct will hold the handler per address
        struct HandlerHolder;
        // This struct will hold each handler
        using HandlerNode = HandlerNode<Interface, Traits, HandlerHolder>;
        // Defines how handler holders are stored (will be some sort of map-like structure from id -> handler holder)
        using AddressStorage = AddressStoragePolicy<Traits, HandlerHolder>;
        // No need for HandlerStorage, there's only 1 so it will always just be a HandlerNode*

        using Handler = IdHandler<Interface, Traits, ContainerType>;
        using MultiHandler = MultiHandler<Interface, Traits, ContainerType>;
        using BusPtr = eastl::intrusive_ptr<HandlerHolder>;

        EBusContainer() = default;

        struct HandlerHolder
        {
            ContainerType& m_busContainer;
            IdType m_busId;
            HandlerNode* m_handler = nullptr;
            // Cache of the interface to save an indirection to m_handler
            Interface* m_interface = nullptr;
            eastl::atomic<unsigned int> m_refCount{ 0 };

            HandlerHolder(ContainerType& storage, const IdType& id)
                : m_busContainer(storage)
                , m_busId(id)
            { }

            HandlerHolder(HandlerHolder&& rhs)
                : m_busContainer(rhs.m_busContainer)
                , m_busId(rhs.m_busId)
                , m_handler(eastl::move(rhs.m_handler))
                , m_interface(eastl::move(rhs.m_interface))
            {
                m_refCount.store(rhs.m_refCount.load());
                rhs.m_refCount.store(0);
            }


            HandlerHolder(const HandlerHolder&) = delete;
            HandlerHolder& operator=(const HandlerHolder&) = delete;
            HandlerHolder& operator=(HandlerHolder&&) = delete;

            ~HandlerHolder() = default;

            // Returns true if this HandlerHolder actually has handers.
            // This will return false when this id is Bind'ed to, but there are no handlers.
            bool HasHandlers() const
            {
                return m_interface != nullptr;
            }

            void AddRef()
            {
                m_refCount.fetch_add(1);
            }
            void Release()
            {
                // Must check against 1 because fetch_sub returns the value before decrementing
                if (m_refCount.fetch_sub(1) == 1)
                {
                    m_busContainer.m_addresses.erase(m_busId);
                }
            }

            struct Hash
            {
                size_t operator()(const HandlerHolder& val) const
                {
                    return eastl::hash<IdType>(val.m_busId);
                }
            };

            struct Equal
            {
                bool operator()(const HandlerHolder& a, const HandlerHolder& b) const
                {
                    return a.m_busId == b.m_busId;
                }
            };
        };

        void Bind(BusPtr& busPtr, const IdType& id)
        {
            busPtr = &FindOrCreateHandlerHolder(id);
        }

        HandlerHolder& FindOrCreateHandlerHolder(const IdType& id)
        {
            auto addressIt = m_addresses.find(id);
            if (addressIt == m_addresses.end())
            {
                addressIt = m_addresses.emplace(eastl::pair<IdType, HandlerHolder>(
                    id, 
                    HandlerHolder(*this, id))
                );
            }

            return addressIt->second;
        }

        void Connect(HandlerNode& handler, const IdType& id)
        {
            assert(!handler.m_holder &&
                "Internal error: Connect() may not be called by a handler that is already connected."
                "BusConnect() should already have handled this case.");

            HandlerHolder& holder = FindOrCreateHandlerHolder(id);
            holder.m_handler = &handler;
            holder.m_interface = handler.m_interface;
            handler.m_holder = &holder;
        }

        void Disconnect(HandlerNode& handler)
        {
            assert(handler.m_holder && "Internal error: disconnecting handler that is incompletely connected");

            handler.m_holder->m_handler = nullptr;
            handler.m_holder->m_interface = nullptr;

            // Must reset handler after removing it from the list, otherwise m_holder could have been destroyed already (and handlerList would be invalid)
            handler.m_holder.reset();
        }

        // set of HandlerHolder
        typename AddressStorage::StorageType m_addresses;
    };

    // Specialization for single address, multi handler
    template <typename Interface, typename Traits, EBusHandlerPolicy handlerPolicy>
    struct EBusContainer<Interface, Traits, EBusAddressPolicy::Single, handlerPolicy>
    {
    public:
        using ContainerType = EBusContainer;
        using IdType = typename Traits::BusIdType;

        // This struct will hold the handlers per address
        struct HandlerHolder;
        // This struct will hold each handler
        using HandlerNode = HandlerNode<Interface, Traits, HandlerHolder>;
        // Defines how handlers are stored per address (will be some sort of list)
        using HandlerStorage = HandlerStoragePolicy<Interface, Traits, HandlerNode>;
        // No need for AddressStorage, there's only 1

        struct BusPtr { };
        using Handler = NonIdHandler<Interface, Traits, ContainerType>;

        EBusContainer() = default;

        void Connect(HandlerNode& handler, const IdType&)
        {
            // Don't need to check for duplicates here, because BusConnect would have caught it already
            m_handlers.insert(handler);
        }

        void Disconnect(HandlerNode& handler)
        {
            // Don't need to check that handler is already connected here, because BusDisconnect would have caught it already
            m_handlers.erase(handler);
        }
        // list or set of HandlerNode
        typename HandlerStorage::StorageType m_handlers;
    };

    // Specialization for single address, single handler
    template <typename Interface, typename Traits>
    struct EBusContainer<Interface, Traits, EBusAddressPolicy::Single, EBusHandlerPolicy::Single>
    {
    public:
        using ContainerType = EBusContainer;
        using IdType = typename Traits::BusIdType;

        // Handlers can just be stored as raw pointer
        // Needs to be public for ConnectionPolicy
        using HandlerNode = Interface*;
        // No need for AddressStorage, there's only 1
        // No need for HandlerStorage, there's only 1

        using Handler = NonIdHandler<Interface, Traits, ContainerType>;
        struct BusPtr { };

        EBusContainer() = default;

        void Connect(HandlerNode& handler, const IdType&)
        {
            assert(!m_handler && "Bus already connected to!");
            m_handler = handler;
        }

        void Disconnect(HandlerNode& handler)
        {
            if (m_handler == handler)
            {
                m_handler = nullptr;
            }
        }

        HandlerNode m_handler = nullptr;
    };
}


