#pragma once

#include <EASTL/allocator.h>
#include <EASTL/type_traits.h>
#include <thread>
#include <mutex>

#include <EASTLEX/hash.h>

#include "Private/CallstackEntry.h"
#include "Private/Polices.h"
#include "Private/EBusImpl.h"

namespace Spark
{
    struct EBusTraits
    {
    protected:

        /**
         * Note - the destructor is intentionally not virtual to avoid adding vtable overhead to every EBusTraits derived class.
         */
        ~EBusTraits() = default;

    public:

        using AllocatorType = eastl::allocator;

        static constexpr EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;

        static constexpr EBusAddressPolicy AddressPolicy = EBusAddressPolicy::Single;

        using BusIdType = NullBusId;

        using BusIdOrderCompare = NullBusIdCompare;

        using BusHandlerOrderCompare = BusHandlerCompareDefault;

        using MutexType = NullMutex;

        static constexpr bool EnableEventQueue = false;
        static constexpr bool EventQueueingActiveByDefault = true;
        static constexpr bool EnableQueuedReferences = false;  // unused

        using EventQueueMutexType = NullMutex;

        static constexpr bool LocklessDispatch = false;
        
        /*
        * \note Make sure you carefully consider the implication of switching this policy. If your code use EBusEnvironments and your storage policy is not
        * complaint in the best case you will cause contention and unintended communication across environments, separation is a goal of environments. In the worst
        * case when you have listeners, you can receive messages when you environment is NOT active, potentially causing all kinds of havoc especially if you execute
        * environments in parallel.
        */
        template <typename Context>
        using StoragePolicy = EBusGlobalStoragePolicy<Context>;

        using EventProcessingPolicy = EBusEventProcessingPolicy;

        template <typename DispatchMutex, bool IsLocklessDispatch>
        using DispatchLockGuard = eastl::conditional_t<
            IsLocklessDispatch, 
            NullLockGuard<DispatchMutex>, 
            std::scoped_lock<DispatchMutex>>;

        template<typename ContextMutex>
        using ConnectLockGuard = eastl::conditional_t<
            eastl::is_same_v<ContextMutex, NullMutex>,
            NullLockGuard<ContextMutex>,
            std::unique_lock<ContextMutex>>;

        template<typename ContextMutex>
        using BindLockGuard = std::scoped_lock<ContextMutex>;

        template<typename ContextMutex>
        using CallstackTrackerLockGuard = eastl::conditional_t<
            eastl::is_same_v<ContextMutex, NullMutex>,
            NullLockGuard<ContextMutex>,
            std::unique_lock<ContextMutex>>;
    };

    template<class Interface, class BusTraits = Interface>
    class EBus
        : public EBusImpl<EBus<Interface, BusTraits>, EBusImplTraits<Interface, BusTraits>, typename BusTraits::BusIdType>
    {
    public:
        class Context;
        
        using ImplTraits = EBusImplTraits<Interface, BusTraits>;

        using BaseImpl = EBusImpl<EBus<Interface, BusTraits>, EBusImplTraits<Interface, BusTraits>, typename BusTraits::BusIdType>;

        using Traits = typename ImplTraits::Traits;

        using ThisType = EBus<Interface, Traits>;

        using AllocatorType = typename ImplTraits::AllocatorType;

        using InterfaceType = typename ImplTraits::InterfaceType;

        using Events = typename ImplTraits::Events;

        using BusIdType = typename ImplTraits::BusIdType;

        using BusIdOrderCompare = typename ImplTraits::BusIdOrderCompare;

        using MutexType = typename ImplTraits::MutexType;

        using BusesContainer = typename ImplTraits::BusesContainer;

        using EventQueueMutexType = typename ImplTraits::EventQueueMutexType;

        using BusPtr = typename ImplTraits::BusPtr;

        using HandlerNode = typename ImplTraits::HandlerNode;

        using QueuePolicy = EBusQueuePolicy<Traits::EnableEventQueue, ThisType, EventQueueMutexType>;

        using CallstackEntry = CallstackEntry<Interface, Traits>;

        static const bool EnableEventQueue = ImplTraits::EnableEventQueue;

        static const bool HasId = Traits::AddressPolicy != EBusAddressPolicy::Single;

        template <typename DispatchMutex>
        using DispatchLockGuardTemplate = typename ImplTraits::template DispatchLockGuard<DispatchMutex>;

        template<typename ContextMutexType>
        using ConnectLockGuardTemplate = typename ImplTraits::template ConnectLockGuard<ContextMutexType>;

        template<typename ContextMutexType>
        using BindLockGuardTemplate = typename ImplTraits::template BindLockGuard<ContextMutexType>;

        template<typename ContextMutexType>
        using CallstackTrackerLockGuardTemplate = typename ImplTraits::template CallstackTrackerLockGuard<ContextMutexType>;


        static_assert((HasId || eastl::is_same<BusIdType, NullBusId>::value),
            "When you use EBusAddressPolicy::Single there is no need to define BusIdType!");
        static_assert((!HasId || !eastl::is_same<BusIdType, NullBusId>::value),
            "You must provide a valid BusIdType when using EBusAddressPolicy::ById or EBusAddressPolicy::ByIdAndOrdered! (ex. using BusIdType = int;");
        static_assert((BusTraits::AddressPolicy == EBusAddressPolicy::ByIdAndOrdered || eastl::is_same<BusIdOrderCompare, NullBusIdCompare>::value),
            "When you use EBusAddressPolicy::Single or EBusAddressPolicy::ById there is no need to define BusIdOrderCompare!");
        static_assert((BusTraits::AddressPolicy != EBusAddressPolicy::ByIdAndOrdered || !eastl::is_same<BusIdOrderCompare, NullBusIdCompare>::value),
            "When you use EBusAddressPolicy::ByIdAndOrdered you must define BusIdOrderCompare (ex. using BusIdOrderCompare = eastl::less<BusIdType>)");

        class Context
        {
            friend ThisType;
            friend CallstackEntry;
        public:
            using ContextMutexType = eastl::conditional_t<BusTraits::LocklessDispatch && eastl::is_same_v<MutexType, NullMutex>, std::shared_mutex, MutexType>;

            using DispatchLockGuard = DispatchLockGuardTemplate<ContextMutexType>;

            using ConnectLockGuard = ConnectLockGuardTemplate<ContextMutexType>;

            using BindLockGuard = BindLockGuardTemplate<ContextMutexType>;

            using CallstackTrackerLockGuard = CallstackTrackerLockGuardTemplate<ContextMutexType>;

            BusesContainer          m_buses;         ///< The actual bus container, which is a static map for each bus type.
            ContextMutexType        m_contextMutex;  ///< Mutex to control access when modifying the context
            QueuePolicy             m_queue;

            Context() = default;
            //Context(EBusEnvironment* environment);
            virtual ~Context() = default;

            // Disallow all copying/moving
            Context(const Context&) = delete;
            Context(Context&&) = delete;
            Context& operator=(const Context&) = delete;
            Context& operator=(Context&&) = delete;

        private:
            using CallstackEntryBase = CallstackEntryBase<Interface, Traits>;
            using CallstackEntryRoot = CallstackEntryRoot<Interface, Traits>;
            using CallstackEntryStorageType = EBusCallstackStorage<CallstackEntryBase, !eastl::is_same_v<ContextMutexType, NullMutex>>;

            mutable eastl::unordered_map<
                std::thread::id,
                CallstackEntryRoot, 
                eastl::hash<std::thread::id>, 
                eastl::equal_to<std::thread::id>, 
                AllocatorType
                > m_callstackRoots;
            CallstackEntryStorageType s_callstack;    ///< Linked list of other bus calls to this bus on the stack, per thread if MutexType is defined
            eastl::atomic<unsigned int> m_dispatches;   ///< Number of active dispatches in progress
        };

        using StoragePolicy = typename Traits::template StoragePolicy<Context>;

        inline static Context& GetOrCreateContext(bool trackCallstack=true)
        {
            Context& context = StoragePolicy::GetOrCreate();
            if (trackCallstack && !context.s_callstack)
            {
                // Cache the callstack root into this thread/dll. Even though s_callstack is thread-local, we need a mutex lock
                // for the modifications to m_callstackRoots, which is NOT thread-local.
                typename Context::CallstackTrackerLockGuard lock(context.m_contextMutex);
                context.s_callstack = &context.m_callstackRoots[std::this_thread::get_id()];
            }
            return context;
        }

        inline static Context* GetContext(bool trackCallstack=true)
        {
            Context* context = StoragePolicy::Get();
            if (trackCallstack && context && !context->s_callstack)
            {
                // Cache the callstack root into this thread/dll. Even though s_callstack is thread-local, we need a mutex lock
                // for the modifications to m_callstackRoots, which is NOT thread-local.
                typename Context::CallstackTrackerLockGuard lock(context->m_contextMutex);
                context->s_callstack = &context->m_callstackRoots[std::this_thread::get_id()];
            }
            return context;
        }

        inline static bool IsInDispatch(Context* context = GetContext(false))
        {
            return context != nullptr && context->m_dispatches > 0;
        }

        inline static bool IsInDispatchThisThread(Context* context = GetContext(false))
        {
            return context != nullptr && context->s_callstack != nullptr
                && context->s_callstack->m_prev != nullptr;
        }

        inline static size_t GetTotalNumOfEventHandlers()
        {
            size_t size = 0;
            BaseImpl::EnumerateHandlers([&size](Interface*)
            {
                ++size;
                return true;
            });
            return size;
        }

        inline static bool HasHandlers()
        {
            bool hasHandlers = false;
            auto findFirstHandler = [&hasHandlers](InterfaceType*)
            {
                hasHandlers = true;
                return false;
            };
            BaseImpl::EnumerateHandlers(findFirstHandler);
            return hasHandlers;
        }

        inline static bool HasHandlers(const BusIdType& id)
        {
            return BaseImpl::FindFirstHandler(id) != nullptr;
        }

        inline static bool HasHandlers(const BusPtr& ptr)
        {
            return BaseImpl::FindFirstHandler(ptr) != nullptr;
        }

        using ConnectLockGuard = typename Context::ConnectLockGuard;

        inline static void Connect(HandlerNode& handler, const BusIdType& id = 0)
        {
            Context& context = GetOrCreateContext();
            // scoped lock guard in case of exception / other odd situation
            // Context mutex is separate from the Dispatch lock guard and therefore this is safe to lock this mutex while in the middle of a dispatch
            ConnectLockGuard lock(context.m_contextMutex);
            ConnectInternal(context, handler, lock, id);
        }

        inline static const BusIdType* GetCurrentBusId()
        {
            Context* context = GetContext();
            if (IsInDispatchThisThread(context))
            {
                return context->s_callstack->m_prev->m_busId;
            }
            return nullptr;
        }

        bool static HasReentrantEBusUseThisThread(const BusIdType* busId)
        {
            Context* context = GetContext();

            if (busId && IsInDispatchThisThread(context))
            {
                bool busIdInCallstack = false;

                // If we're in a dispatch, callstack->m_prev contains the entry for the current bus call. Start the search for the given
                // bus ID and look upwards. If we find the given ID more than once in the callstack, we've got a reentrant call.
                for (auto callstackEntry = context->s_callstack->m_prev; callstackEntry != nullptr; callstackEntry = callstackEntry->m_prev)
                {
                    if ((*busId) == (*callstackEntry->m_busId))
                    {
                        if (busIdInCallstack)
                        {
                            return true;
                        }

                        busIdInCallstack = true;
                    }
                }
            }

            return false;
        }

        inline static void ConnectInternal(Context& context, HandlerNode& handler, ConnectLockGuard& contextLock, const BusIdType& id)
        {
            // To call this while executing a message, you need to make sure this mutex is recursive_mutex. Otherwise, a deadlock will occur.
            assert(!Traits::LocklessDispatch || !IsInDispatch(&context) && "It is not safe to connect during dispatch on a lockless dispatch EBus");

            // Do the actual connection
            context.m_buses.Connect(handler, id);

            BusPtr ptr;
            if constexpr (EBus::HasId)
            {
                ptr = handler.m_holder;
            }
            CallstackEntry entry(&context, &id);
        }

        inline static void Disconnect(HandlerNode& handler)
        {
            if (Context* context = GetContext())
            {
                // scoped lock guard in case of exception / other odd situation
                ConnectLockGuard lock(context->m_contextMutex);
                DisconnectInternal(*context, handler);
            }
        }

        inline static void DisconnectInternal(Context& context, HandlerNode& handler)
        {
            // To call this while executing a message, you need to make sure this mutex is recursive_mutex. Otherwise, a deadlock will occur.
            assert(!Traits::LocklessDispatch || !IsInDispatch(&context) && "It is not safe to disconnect during dispatch on a lockless dispatch EBus");

            auto callstack = context.s_callstack->m_prev;
            if (callstack)
            {
                callstack->OnRemoveHandler(handler);
            }

            BusPtr ptr;
            if constexpr (EBus::HasId)
            {
                ptr = handler.m_holder;
            }

            CallstackEntry entry(&context, nullptr);

            // Do the actual disconnection
            context.m_buses.Disconnect(handler);

            if (callstack)
            {
                callstack->OnPostRemoveHandler();
            }

            handler = nullptr;
        }
    };
}