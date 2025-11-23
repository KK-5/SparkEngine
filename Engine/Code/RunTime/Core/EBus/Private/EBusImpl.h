#pragma once

#include "Dispatcher.h"
#include "QueueDispatcher.h"

namespace Spark
{
    /**
     * A dummy mutex that performs no locking.
     * EBuses that do not support multithreading use this mutex
     * as their EBusTraits::MutexType.
     */
    struct NullMutex
    {
        void lock() {}
        bool try_lock() { return true; }
        void unlock() {}
    };


    /**
     * Indicates that EBusTraits::BusIdType is not set.
     * EBuses with multiple addresses must set the EBusTraits::BusIdType.
     */
    struct NullBusId
    {
        NullBusId() {};
        NullBusId(int) {};
    };

    /// @cond EXCLUDE_DOCS
    inline bool operator==(const NullBusId&, const NullBusId&) { return true; }
    inline bool operator!=(const NullBusId&, const NullBusId&) { return false; }
    /// @endcond

    /**
     * Indicates that EBusTraits::BusIdOrderCompare is not set.
     * EBuses with ordered address IDs must specify a function for
     * EBusTraits::BusIdOrderCompare.
     */
    struct NullBusIdCompare;

    template <typename Lock>
    struct NullLockGuard
    {
        explicit NullLockGuard(Lock&) {}
        NullLockGuard(Lock&, std::adopt_lock_t) {}

        void lock() {}
        bool try_lock() { return true; }
        void unlock() {}
    };

    template <typename Interface, typename BusTraits>
    struct EBusImplTraits
    {
        using Traits = BusTraits;

        using BaseTraits = BusTraits;

        using AllocatorType = typename Traits::AllocatorType;

        static constexpr EBusHandlerPolicy HandlerPolicy = Traits::HandlerPolicy;

        static constexpr EBusAddressPolicy AddressPolicy = Traits::AddressPolicy;

        using InterfaceType = Interface;

        using Events = Interface;

        using BusIdType = typename Traits::BusIdType;

        using BusIdOrderCompare = typename Traits::BusIdOrderCompare;

        using MutexType = typename Traits::MutexType;

        using BusesContainer = EBusContainer<Interface, Traits>;

        using EventProcessingPolicy = typename Traits::EventProcessingPolicy;

        using EventQueueMutexType = eastl::conditional_t<eastl::is_same<typename Traits::EventQueueMutexType, NullMutex>::value, // if EventQueueMutexType==NullMutex use MutexType otherwise EventQueueMutexType
            MutexType, typename Traits::EventQueueMutexType>;

        using BusPtr = typename BusesContainer::BusPtr;

        using HandlerNode = typename BusesContainer::HandlerNode;

        static constexpr bool EnableEventQueue = Traits::EnableEventQueue;
        static constexpr bool EventQueueingActiveByDefault = Traits::EventQueueingActiveByDefault;
        static constexpr bool EnableQueuedReferences = Traits::EnableQueuedReferences;

        static constexpr bool HasId = Traits::AddressPolicy != EBusAddressPolicy::Single;

        template <typename DispatchMutex>
        using DispatchLockGuard = typename Traits::template DispatchLockGuard<DispatchMutex, Traits::LocklessDispatch>;

        template<typename ContextMutex>
        using ConnectLockGuard = typename Traits::template ConnectLockGuard<ContextMutex>;

        template<typename ContextMutex>
        using BindLockGuard = typename Traits::template BindLockGuard<ContextMutex>;

        template<typename ContextMutex>
        using CallstackTrackerLockGuard = typename Traits::template CallstackTrackerLockGuard<ContextMutex>;
    };

    template <typename Bus, typename Traits, typename BusIdType>
    struct EBusImpl
        : public EBusBroadcaster<Bus, Traits>
        , public EBusEventer<Bus, Traits>
        , public EBusEnumerator<Bus, Traits>
        , public eastl::conditional_t<Traits::EnableEventQueue, EBusEventQueue<Bus, Traits>, EBusNullQueue>
    {
        using Handler = typename Traits::BusesContainer::Handler;
        using MultiHandler = typename Traits::BusesContainer::MultiHandler;
    };

    template <typename Bus, typename Traits>
    struct EBusImpl<Bus, Traits, NullBusId>
        : public EBusBroadcaster<Bus, Traits>
        , public EBusEnumerator<Bus, Traits>
        , public eastl::conditional_t<Traits::EnableEventQueue, EBusBroadcastQueue<Bus, Traits>, EBusNullQueue>
    {
        using Handler = typename Traits::BusesContainer::Handler;
    };
}