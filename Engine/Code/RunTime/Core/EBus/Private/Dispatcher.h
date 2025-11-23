#pragma once

#include <EASTL/functional.h>

#include "Polices.h"
#include "Container.h"
#include "CallstackEntry.h"


namespace Spark
{
    // Handles updating iterators when disconnecting mid-dispatch
    template <typename Bus, typename PreHandler, typename PostHandler>
    class MidDispatchDisconnectFixer
        : public CallstackEntry<typename Bus::InterfaceType, typename Bus::Traits>
    {
        using Base = CallstackEntry<typename Bus::InterfaceType, typename Bus::Traits>;

    public:
        template<typename PreRemoveHandler, typename PostRemoveHandler>
        MidDispatchDisconnectFixer(typename Bus::Context* context, const typename Bus::BusIdType* busId, PreRemoveHandler&& pre, PostRemoveHandler&& post)
            : Base(context, busId)
            , m_onPreDisconnect(eastl::forward<PreRemoveHandler>(pre))
            , m_onPostDisconnect(eastl::forward<PostRemoveHandler>(post))
        {
            static_assert(eastl::is_invocable<PreHandler, typename Bus::InterfaceType*>::value, "Pre handler requires accepting a parameter of Bus::InterfaceType pointer");
            static_assert(eastl::is_invocable<PostHandler>::value, "Post handler does not accept any parameters");
        }

        void OnRemoveHandler(typename Bus::InterfaceType* handler) override
        {
            m_onPreDisconnect(handler);
            Base::OnRemoveHandler(handler);
        }

        void OnPostRemoveHandler() override
        {
            m_onPostDisconnect();
            Base::OnPostRemoveHandler();
        }

    private:
        PreHandler m_onPreDisconnect;
        PostHandler m_onPostDisconnect;
    };

    // Helper for creating a MidDispatchDisconnectFixer, with support for deducing handler types (required for lambdas)
    template <typename Bus, typename PreHandler, typename PostHandler>
    auto MakeDisconnectFixer(typename Bus::Context* context, const typename Bus::BusIdType* busId, PreHandler&& remove, PostHandler&& post)
        -> MidDispatchDisconnectFixer<Bus, PreHandler, PostHandler>
    {
        return MidDispatchDisconnectFixer<Bus, PreHandler, PostHandler>(context, busId, eastl::forward<PreHandler>(remove), eastl::forward<PostHandler>(post));
    }


    // Default impl, used when there are multiple addresses and multiple handlers
    template <typename EBus, typename Traits, EBusAddressPolicy addressPolicy = Traits::AddressPolicy, EBusHandlerPolicy handlerPolicy = Traits::HandlerPolicy>
    struct EBusEventer
    {
    private:
        using Bus = EBus;
        using BusPtr = typename Traits::BusesContainer::BusPtr;
        using IdType = typename Traits::BusIdType;
        using HandlerHolder = typename Traits::BusesContainer::HandlerHolder;
        using InterfaceType = typename Traits::InterfaceType;
        using CallstackEntryType = CallstackEntry<InterfaceType, typename Traits::BaseTraits>;
    public:
        static void Bind(BusPtr& ptr, const IdType& id)
        {
            auto& context = Bus::GetOrCreateContext();
            typename Traits::template BindLockGuard<decltype(context.m_contextMutex)> lock(context.m_contextMutex);
            context.m_buses.Bind(ptr, id);
        }

        template <typename Function, typename... Args>
        static void Event(const IdType& id, Function&& func, Args&&... args)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& addresses = context->m_buses.m_addresses;
                auto addressIt = addresses.find(id);
                if (addressIt != addresses.end())
                {
                    // 这里取了HandlerHolder实例，手动为其增加引用防止其失效
                    HandlerHolder& holder = addressIt->second;
                    holder.AddRef();

                    auto& handlers = holder.m_handlers;
                    auto handlerIt = handlers.begin();
                    auto handlersEnd = handlers.end();

                    auto fixer = MakeDisconnectFixer<Bus>(context, &id,
                        [&handlerIt, &handlersEnd](InterfaceType* handler)
                        {
                            if (handlerIt != handlersEnd && handlerIt->m_interface == handler)
                            {
                                ++handlerIt;
                            }
                        },
                        [&handlers, &handlersEnd]()
                        {
                            handlersEnd = handlers.end();
                        }
                    );

                    while (handlerIt != handlersEnd)
                    {
                        auto itr = handlerIt++;
                        Traits::EventProcessingPolicy::Call(func, *itr, args...);
                    }

                    holder.Release();
                }
            }
        }

        template <typename Results, typename Function, typename... Args>
        static void EventResult(Results& results, const IdType& id, Function&& func, Args&&... args)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& addresses = context->m_buses.m_addresses;
                auto addressIt = addresses.find(id);
                if (addressIt != addresses.end())
                {
                    HandlerHolder& holder = addressIt->second;
                    holder.AddRef();

                    auto& handlers = holder.m_handlers;
                    auto handlerIt = handlers.begin();
                    auto handlersEnd = handlers.end();

                    auto fixer = MakeDisconnectFixer<Bus>(context, &id,
                        [&handlerIt, &handlersEnd](InterfaceType* handler)
                        {
                            if (handlerIt != handlersEnd && handlerIt->m_interface == handler)
                            {
                                ++handlerIt;
                            }
                        },
                        [&handlers, &handlersEnd]()
                        {
                            handlersEnd = handlers.end();
                        }
                    );

                    while (handlerIt != handlersEnd)
                    {
                        auto itr = handlerIt++;
                        Traits::EventProcessingPolicy::CallResult(results, func, *itr, args...);
                    }

                    holder.Release();
                }
            }
        }

        template <typename Function, typename... Args>
        static void Event(const BusPtr& busPtr, Function&& func, Args&&... args)
        {
            if (busPtr)
            {
                auto* context = Bus::GetContext();
                assert(context && "Internal error: context deleted with bind ptr outstanding.");
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& handlers = busPtr->m_handlers;
                auto handlerIt = handlers.begin();
                auto handlersEnd = handlers.end();

                auto fixer = MakeDisconnectFixer<Bus>(context, &busPtr->m_busId,
                    [&handlerIt, &handlersEnd](InterfaceType* handler)
                    {
                        if (handlerIt != handlersEnd && handlerIt->m_interface == handler)
                        {
                            ++handlerIt;
                        }
                    },
                    [&handlers, &handlersEnd]()
                    {
                        handlersEnd = handlers.end();
                    }
                );

                while (handlerIt != handlersEnd)
                {
                    auto itr = handlerIt++;
                    Traits::EventProcessingPolicy::Call(func, *itr, args...);
                }
            }
        }

        template <typename Results, typename Function, typename... Args>
        static void EventResult(Results& results, const BusPtr& busPtr, Function&& func, Args&&... args)
        {
            if (busPtr)
            {
                auto* context = Bus::GetContext();
                assert(context && "Internal error: context deleted with bind ptr outstanding.");
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& handlers = busPtr->m_handlers;
                auto handlerIt = handlers.begin();
                auto handlersEnd = handlers.end();

                auto fixer = MakeDisconnectFixer<Bus>(context, &busPtr->m_busId,
                    [&handlerIt, &handlersEnd](InterfaceType* handler)
                    {
                        if (handlerIt != handlersEnd && handlerIt->m_interface == handler)
                        {
                            ++handlerIt;
                        }
                    },
                    [&handlers, &handlersEnd]()
                    {
                        handlersEnd = handlers.end();
                    }
                );

                while (handlerIt != handlersEnd)
                {
                    auto itr = handlerIt++;
                    Traits::EventProcessingPolicy::CallResult(results, func, *itr, args...);
                }
            }
        }
    };

    // Specialization for multi address, single handler
    template <typename EBus, typename Traits, EBusAddressPolicy addressPolicy>
    struct EBusEventer<EBus, Traits, addressPolicy, EBusHandlerPolicy::Single>
    {
    private:
        using Bus = EBus;
        using BusPtr = typename Traits::BusesContainer::BusPtr;
        using IdType = typename Traits::BusIdType;
        using HandlerHolder = typename Traits::BusesContainer::HandlerHolder;
        using InterfaceType = typename Traits::InterfaceType;
        using CallstackEntryType = CallstackEntry<InterfaceType, typename Traits::BaseTraits>;
    public:
        static void Bind(BusPtr& ptr, const IdType& id)
        {
            auto& context = Bus::GetOrCreateContext();
            typename Traits::template BindLockGuard<decltype(context.m_contextMutex)> lock(context.m_contextMutex);
            context.m_buses.Bind(ptr, id);
        }

        template <typename Function, typename... Args>
        static void Event(const IdType& id, Function&& func, Args&&... args)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& addresses = context->m_buses.m_addresses;
                auto addressIt = addresses.find(id);
                if (addressIt != addresses.end() && addressIt->second.m_interface)
                {
                    CallstackEntryType entry(context, &addressIt->second.m_busId);
                    Traits::EventProcessingPolicy::Call(eastl::forward<Function>(func), addressIt->second.m_interface, eastl::forward<Args>(args)...);
                }
            }
        }

        template <typename Results, typename Function, typename... Args>
        static void EventResult(Results& results, const IdType& id, Function&& func, Args&&... args)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& addresses = context->m_buses.m_addresses;
                auto addressIt = addresses.find(id);
                if (addressIt != addresses.end() && addressIt->second.m_interface)
                {
                    CallstackEntryType entry(context, &addressIt->second.m_busId);
                    Traits::EventProcessingPolicy::CallResult(results, eastl::forward<Function>(func), addressIt->second.m_interface, eastl::forward<Args>(args)...);
                }
            }
        }

        template <typename Function, typename... Args>
        static void Event(const BusPtr& busPtr, Function&& func, Args&&... args)
        {
            if (busPtr)
            {
                auto* context = Bus::GetContext();
                assert(context && "Internal error: context deleted with bind ptr outstanding.");
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                if (busPtr->m_interface)
                {
                    CallstackEntryType entry(context, &busPtr->m_busId);
                    Traits::EventProcessingPolicy::Call(eastl::forward<Function>(func), busPtr->m_interface, eastl::forward<Args>(args)...);
                }
            }
        }

        template <typename Results, typename Function, typename... Args>
        static void EventResult(Results& results, const BusPtr& busPtr, Function&& func, Args&&... args)
        {
            if (busPtr)
            {
                auto* context = Bus::GetContext();
                assert(context && "Internal error: context deleted with bind ptr outstanding.");
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                if (busPtr->m_interface)
                {
                    CallstackEntryType entry(context, &busPtr->m_busId);
                    Traits::EventProcessingPolicy::CallResult(results, eastl::forward<Function>(func), busPtr->m_interface, eastl::forward<Args>(args)...);
                }
            }
        }
    };


    // Default impl, used when there are multiple addresses and multiple handlers
    template <typename EBus, typename Traits, EBusAddressPolicy addressPolicy = Traits::AddressPolicy, EBusHandlerPolicy handlerPolicy = Traits::HandlerPolicy>
    struct EBusBroadcaster
    {
    private:
        using Bus = EBus;
        using InterfaceType = typename Traits::InterfaceType;
        using HandlerHolder = typename Traits::BusesContainer::HandlerHolder;
        using CallstackEntryType = CallstackEntry<InterfaceType, typename Traits::BaseTraits>;
    public:
        template <typename Function, typename... Args>
        static void Broadcast(Function&& func, Args&&... args)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& addresses = context->m_buses.m_addresses;
                auto addressIt = addresses.begin();
                while (addressIt != addresses.end())
                {
                    HandlerHolder& holder = addressIt->second;
                    holder.AddRef();

                    auto& handlers = holder.m_handlers;
                    auto handlerIt = handlers.begin();
                    auto handlersEnd = handlers.end();

                    auto fixer = MakeDisconnectFixer<Bus>(context, &holder.m_busId,
                        [&handlerIt, &handlersEnd](InterfaceType* handler)
                        {
                            if (handlerIt != handlersEnd && handlerIt->m_interface == handler)
                            {
                                ++handlerIt;
                            }
                        },
                        [&handlers, &handlersEnd]()
                        {
                            handlersEnd = handlers.end();
                        }
                    );

                    while (handlerIt != handlersEnd)
                    {
                        auto itr = handlerIt++;
                        Traits::EventProcessingPolicy::Call(func, *itr, args...);
                    }

                    // Increment before release so that if holder goes away, iterator is still valid
                    ++addressIt;

                    holder.Release();
                }
            }
        }

        template <typename Results, typename Function, typename... Args>
        static void BroadcastResult(Results& results, Function&& func, Args&&... args)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& addresses = context->m_buses.m_addresses;
                auto addressIt = addresses.begin();
                while (addressIt != addresses.end())
                {
                    HandlerHolder& holder = addressIt->second;
                    holder.AddRef();

                    auto& handlers = holder.m_handlers;
                    auto handlerIt = handlers.begin();
                    auto handlersEnd = handlers.end();

                    auto fixer = MakeDisconnectFixer<Bus>(context, &holder.m_busId,
                        [&handlerIt, &handlersEnd](InterfaceType* handler)
                        {
                            if (handlerIt != handlersEnd && handlerIt->m_interface == handler)
                            {
                                ++handlerIt;
                            }
                        },
                        [&handlers, &handlersEnd]()
                        {
                            handlersEnd = handlers.end();
                        }
                    );

                    while (handlerIt != handlersEnd)
                    {
                        auto itr = handlerIt++;
                        Traits::EventProcessingPolicy::CallResult(results, func, *itr, args...);
                    }

                    // Increment before release so that if holder goes away, iterator is still valid
                    ++addressIt;

                    holder.Release();
                }
            }
        }
    };

    // Specialization for multi address, single handler
    template <typename EBus, typename Traits, EBusAddressPolicy addressPolicy>
    struct EBusBroadcaster<EBus, Traits, addressPolicy, EBusHandlerPolicy::Single>
    {
        using Bus = EBus;
        using InterfaceType = typename Traits::InterfaceType;
        using CallstackEntryType = CallstackEntry<InterfaceType, typename Traits::BaseTraits>;

        template <typename Function, typename... Args>
        static void Broadcast(Function&& func, Args&&... args)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& addresses = context->m_buses.m_addresses;
                auto addressIt = addresses.begin();
                auto addressesEnd = addresses.end();

                auto fixer = MakeDisconnectFixer<Bus>(context, nullptr,
                    [&addressIt, &addressesEnd](InterfaceType* handler)
                    {
                        if (addressIt != addressesEnd && addressIt->second.m_handler->m_interface == handler)
                        {
                            ++addressIt;
                        }
                    },
                    [&addresses, &addressesEnd]()
                    {
                        addressesEnd = addresses.end();
                    }
                );

                while (addressIt != addressesEnd)
                {
                    fixer.m_busId = &addressIt->second.m_busId;
                    if (InterfaceType* inst = (addressIt++)->second.m_interface)
                    {
                        // @func and @args cannot be forwarded here as rvalue arguments need to bind to const lvalue arguments
                        // due to potential of multiple addresses of this EBus container invoking the function multiple times
                        Traits::EventProcessingPolicy::Call(func, inst, args...);
                    }
                }
            }
        }

        template <typename Results, typename Function, typename... Args>
        static void BroadcastResult(Results& results, Function&& func, Args&&... args)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& addresses = context->m_buses.m_addresses;
                auto addressIt = addresses.begin();
                auto addressesEnd = addresses.end();

                auto fixer = MakeDisconnectFixer<Bus>(context, nullptr,
                    [&addressIt, &addressesEnd](InterfaceType* handler)
                    {
                        if (addressIt != addressesEnd && addressIt->second.m_handler->m_interface == handler)
                        {
                            ++addressIt;
                        }
                    },
                    [&addresses, &addressesEnd]()
                    {
                        addressesEnd = addresses.end();
                    }
                );

                while (addressIt != addressesEnd)
                {
                    fixer.m_busId = &addressIt->second.m_busId;
                    if (InterfaceType* inst = (addressIt++)->second.m_interface)
                    {
                        // @func and @args cannot be forwarded here as rvalue arguments need to bind to const lvalue arguments
                        // due to potential of multiple addresses of this EBus container invoking the function multiple times
                        Traits::EventProcessingPolicy::CallResult(results, func, inst, args...);
                    }
                }
            }
        }
    };

    // Specialization for single address, multi handler
    template <typename EBus, typename Traits, EBusHandlerPolicy handlerPolicy>
    struct EBusBroadcaster<EBus, Traits, EBusAddressPolicy::Single, handlerPolicy>
    {
    private:
        using Bus = EBus;
        using InterfaceType = typename Traits::InterfaceType;
        using CallstackEntryType = CallstackEntry<InterfaceType, typename Traits::BaseTraits>;
    public:
        template <typename Function, typename... Args>
        static void Broadcast(Function&& func, Args&&... args)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& handlers = context->m_buses.m_handlers;
                auto handlerIt = handlers.begin();
                auto handlersEnd = handlers.end();

                auto fixer = MakeDisconnectFixer<Bus>(context, nullptr,
                    [&handlerIt, &handlersEnd](InterfaceType* handler)
                    {
                        if (handlerIt != handlersEnd && handlerIt->m_interface == handler)
                        {
                            ++handlerIt;
                        }
                    },
                    [&handlers, &handlersEnd]()
                    {
                        handlersEnd = handlers.end();
                    }
                );

                while (handlerIt != handlersEnd)
                {
                    // @func and @args cannot be forwarded here as rvalue arguments need to bind to const lvalue arguments
                    // due to potential of multiple handlers of this EBus container invoking the function multiple times
                    auto itr = handlerIt++;
                    Traits::EventProcessingPolicy::Call(func, *itr, args...);
                }
            }
        }

        template <typename Results, typename Function, typename... Args>
        static void BroadcastResult(Results& results, Function&& func, Args&&... args)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& handlers = context->m_buses.m_handlers;
                auto handlerIt = handlers.begin();
                auto handlersEnd = handlers.end();

                auto fixer = MakeDisconnectFixer<Bus>(context, nullptr,
                    [&handlerIt, &handlersEnd](InterfaceType* handler)
                    {
                        if (handlerIt != handlersEnd && handlerIt->m_interface == handler)
                        {
                            ++handlerIt;
                        }
                    },
                    [&handlers, &handlersEnd]()
                    {
                        handlersEnd = handlers.end();
                    }
                );

                while (handlerIt != handlersEnd)
                {
                    // @func and @args cannot be forwarded here as rvalue arguments need to bind to const lvalue arguments
                    // due to potential of multiple handlers of this EBus container invoking the function multiple times
                    auto itr = handlerIt++;
                    Traits::EventProcessingPolicy::CallResult(results, func, *itr, args...);
                }
            }
        }
    };

    // Specialization for single address, single handler
    template <typename EBus, typename Traits>
    struct EBusBroadcaster<EBus, Traits, EBusAddressPolicy::Single, EBusHandlerPolicy::Single>
    {
    private:
        using Bus = EBus;
        using InterfaceType = typename Traits::InterfaceType;
        using CallstackEntryType = CallstackEntry<InterfaceType, typename Traits::BaseTraits>;
    public:
        template <typename Function, typename... Args>
        static void Broadcast(Function&& func, Args&&... args)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto handler = context->m_buses.m_handler;
                if (handler)
                {
                    CallstackEntryType entry(context, nullptr);
                    Traits::EventProcessingPolicy::Call(eastl::forward<Function>(func), handler, eastl::forward<Args>(args)...);
                }
            }
        }

        template <typename Results, typename Function, typename... Args>
        static void BroadcastResult(Results& results, Function&& func, Args&&... args)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto handler = context->m_buses.m_handler;
                if (handler)
                {
                    CallstackEntryType entry(context, nullptr);
                    Traits::EventProcessingPolicy::CallResult(results, eastl::forward<Function>(func), handler, eastl::forward<Args>(args)...);
                }
            }
        }
    };


    // Default impl, used when there are multiple addresses and multiple handlers
    template <typename EBus, typename Traits, EBusAddressPolicy addressPolicy = Traits::AddressPolicy, EBusHandlerPolicy handlerPolicy = Traits::HandlerPolicy>
    struct EBusEnumerator
    {
    private:
        using Bus = EBus;
        using BusPtr = typename Traits::BusesContainer::BusPtr;
        using HandlerHolder = typename Traits::BusesContainer::HandlerHolder;
        using IdType = typename Traits::BusIdType;
        using InterfaceType = typename Traits::InterfaceType;
        using CallstackEntryType = CallstackEntry<InterfaceType, typename Traits::BaseTraits>;
    public:
        template <typename Callback>
        static void EnumerateHandlers(Callback&& callback)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& addresses = context->m_buses.m_addresses;
                auto addressIt = addresses.begin();
                auto addressesEnd = addresses.end();
                while (addressIt != addressesEnd)
                {
                    if (!EnumerateHandlersImpl(context, (addressIt++)->second, eastl::forward<Callback>(callback)))
                    {
                        break;
                    }
                }
            }
        }

        template <typename Callback>
        static void EnumerateHandlersId(const IdType& id, Callback&& callback)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& addresses = context->m_buses.m_addresses;
                auto addressIt = addresses.find(id);
                if (addressIt != addresses.end())
                {
                    EnumerateHandlersImpl(context, addressIt->second, eastl::forward<Callback>(callback));
                }
            }
        }

        template <typename Callback>
        static void EnumerateHandlersPtr(const BusPtr& ptr, Callback&& callback)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                if (ptr)
                {
                    EnumerateHandlersImpl(context, *ptr, eastl::forward<Callback>(callback));
                }
            }
        }

        static InterfaceType* FindFirstHandler(const IdType& id)
        {
            InterfaceType* result = nullptr;
            EnumerateHandlersId(id, [&result](InterfaceType* handler)
            {
                result = handler;
                return false;
            });
            return result;
        }

        static InterfaceType* FindFirstHandler(const BusPtr& ptr)
        {
            InterfaceType* result = nullptr;
            Bus::EnumerateHandlersPtr(ptr, [&result](InterfaceType* handler)
            {
                result = handler;
                return false;
            });
            return result;
        }

        static size_t GetNumOfEventHandlers(const IdType& id)
        {
            size_t size = 0;
            Bus::EnumerateHandlersId(id, [&size](InterfaceType*)
            {
                ++size;
                return true;
            });
            return size;
        }
    
    private:
        // All enumerate functions do basically the same thing once they have a holder, so implement it here
        template <typename Callback>
        static bool EnumerateHandlersImpl(void* context, HandlerHolder& holder, Callback&& callback)
        {
            auto& handlers = holder.m_handlers;
            auto handlerIt = handlers.begin();
            auto handlersEnd = handlers.end();

            // This must be done via void* and static cast because the EBus type
            // is not available for resolution while function signatures are compiled.
            //using BusType = EBus<InterfaceType, Traits>;
            auto fixer = MakeDisconnectFixer<Bus>(static_cast<typename Bus::Context*>(context), &holder.m_busId,
                [&handlerIt, &handlersEnd](InterfaceType* handler)
                {
                    if (handlerIt != handlersEnd && handlerIt->m_interface == handler)
                    {
                        ++handlerIt;
                    }
                },
                [&handlers, &handlersEnd]()
                {
                    handlersEnd = handlers.end();
                }
            );

            bool shouldContinue = true;
            while (handlerIt != handlersEnd)
            {
                bool result = false;
                auto itr = handlerIt++;
                Traits::EventProcessingPolicy::CallResult(result, callback, itr->m_interface);

                if (!result)
                {
                    shouldContinue = false;
                    break;
                }
            }

            return shouldContinue;
        }
    };

    // Specialization for multi address, single handler
    template <typename EBus, typename Traits, EBusAddressPolicy addressPolicy>
    struct EBusEnumerator<EBus, Traits, addressPolicy, EBusHandlerPolicy::Single>
    {
    private:
        using Bus = EBus;
        using BusPtr = typename Traits::BusesContainer::BusPtr;
        using IdType = typename Traits::BusIdType;
        using InterfaceType = typename Traits::InterfaceType;
        using CallstackEntryType = CallstackEntry<InterfaceType, typename Traits::BaseTraits>;
    public:
        template <typename Callback>
        static void EnumerateHandlers(Callback&& callback)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& addresses = context->m_buses.m_addresses;
                auto addressIt = addresses.begin();
                auto addressesEnd = addresses.end();

                auto fixer = MakeDisconnectFixer<Bus>(Bus::GetContext(), nullptr,
                    [&addressIt, &addressesEnd](InterfaceType* handler)
                    {
                        if (addressIt != addressesEnd && addressIt->second.m_handler->m_interface == handler)
                        {
                            ++addressIt;
                        }
                    },
                    [&addresses, &addressesEnd]()
                    {
                        addressesEnd = addresses.end();
                    }
                );

                while (addressIt != addressesEnd)
                {
                    fixer.m_busId = &addressIt->second.m_busId;
                    if (InterfaceType* inst = (addressIt++)->second.m_interface)
                    {
                        bool result = false;
                        Traits::EventProcessingPolicy::CallResult(result, callback, inst);
                        if (!result)
                        {
                            return;
                        }
                    }
                }
            }
        }

        template <typename Callback>
        static void EnumerateHandlersId(const IdType& id, Callback&& callback)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& addresses = context->m_buses.m_addresses;
                auto addressIt = addresses.find(id);
                if (addressIt != addresses.end())
                {
                    if (InterfaceType* inst = addressIt->second.m_interface)
                    {
                        CallstackEntryType entry(context, &id);
                        bool result = false;
                        Traits::EventProcessingPolicy::CallResult(result, callback, inst);
                        if (!result)
                        {
                            return;
                        }
                    }
                }
            }
        }

        template <typename Callback>
        static void EnumerateHandlersPtr(const BusPtr& ptr, Callback&& callback)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                if (ptr)
                {
                    if (InterfaceType* inst = ptr->m_interface)
                    {
                        CallstackEntryType entry(context, &ptr->m_busId);
                        bool result = false;
                        Traits::EventProcessingPolicy::CallResult(result, callback, inst);
                        if (!result)
                        {
                            return;
                        }
                    }
                }
            }
        }

        static InterfaceType* FindFirstHandler(const IdType& id)
        {
            InterfaceType* result = nullptr;
            EnumerateHandlersId(id, [&result](InterfaceType* handler)
            {
                result = handler;
                return false;
            });
            return result;
        }

        static InterfaceType* FindFirstHandler(const BusPtr& ptr)
        {
            InterfaceType* result = nullptr;
            Bus::EnumerateHandlersPtr(ptr, [&result](InterfaceType* handler)
            {
                result = handler;
                return false;
            });
            return result;
        }

        static size_t GetNumOfEventHandlers(const IdType& id)
        {
            size_t size = 0;
            Bus::EnumerateHandlersId(id, [&size](InterfaceType*)
            {
                ++size;
                return true;
            });
            return size;
        }
    };

    // Specialization for single address, multi handler
    template <typename EBus, typename Traits, EBusHandlerPolicy handlerPolicy>
    struct EBusEnumerator<EBus, Traits, EBusAddressPolicy::Single, handlerPolicy>
    {
    private:
        using Bus = EBus;
        using BusPtr = typename Traits::BusesContainer::BusPtr;
        using IdType = typename Traits::BusIdType;
        using InterfaceType = typename Traits::InterfaceType;
        using CallstackEntryType = CallstackEntry<InterfaceType, typename Traits::BaseTraits>;
    public:
        template <typename Callback>
        static void EnumerateHandlers(Callback&& callback)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto& handlers = context->m_buses.m_handlers;
                auto handlerIt = handlers.begin();
                auto handlersEnd = handlers.end();

                auto fixer = MakeDisconnectFixer<Bus>(context, nullptr,
                    [&handlerIt, &handlersEnd](InterfaceType* handler)
                    {
                        if (handlerIt != handlersEnd && handlerIt->m_interface == handler)
                        {
                            ++handlerIt;
                        }
                    },
                    [&handlers, &handlersEnd]()
                    {
                        handlersEnd = handlers.end();
                    }
                );

                while (handlerIt != handlersEnd)
                {
                    bool result = false;
                    auto itr = handlerIt++;
                    Traits::EventProcessingPolicy::CallResult(result, callback, itr->m_interface);
                    if (!result)
                    {
                        return;
                    }
                }
            }
        }

        static InterfaceType* FindFirstHandler()
        {
            InterfaceType* result = nullptr;
            Bus::EnumerateHandlers([&result](InterfaceType* handler)
            {
                result = handler;
                return false;
            });
            return result;
        }

        static InterfaceType* FindFirstHandler([[maybe_unused]] const IdType& id)
        {
            return FindFirstHandler();
        }

        static InterfaceType* FindFirstHandler([[maybe_unused]]const BusPtr& ptr)
        {
            return FindFirstHandler();
        }
    };

    // Specialization for single address, single handler
    template <typename EBus, typename Traits>
    struct EBusEnumerator<EBus, Traits, EBusAddressPolicy::Single, EBusHandlerPolicy::Single>
    {
    private:
        using Bus = EBus;
        using BusPtr = typename Traits::BusesContainer::BusPtr;
        using IdType = typename Traits::BusIdType;
        using InterfaceType = typename Traits::InterfaceType;
        using CallstackEntryType = CallstackEntry<InterfaceType, typename Traits::BaseTraits>;
    public:
        template <typename Callback>
        static void EnumerateHandlers(Callback&& callback)
        {
            if (auto* context = Bus::GetContext())
            {
                typename Bus::Context::DispatchLockGuard lock(context->m_contextMutex);

                auto handler = context->m_buses.m_handler;
                if (handler)
                {
                    CallstackEntryType entry(context, nullptr);
                    Traits::EventProcessingPolicy::Call(callback, handler);
                }
            }
        }

        static InterfaceType* FindFirstHandler()
        {
            InterfaceType* result = nullptr;
            Bus::EnumerateHandlers([&result](InterfaceType* handler)
            {
                result = handler;
                return false;
            });
            return result;
        }

        static InterfaceType* FindFirstHandler([[maybe_unused]] const IdType& id)
        {
            return FindFirstHandler();
        }

        static InterfaceType* FindFirstHandler([[maybe_unused]]const BusPtr& ptr)
        {
            return FindFirstHandler();
        }
    };
}