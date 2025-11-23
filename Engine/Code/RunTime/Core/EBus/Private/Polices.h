#pragma once

#include <EASTL/functional.h>
#include <EASTL/queue.h>

#include <mutex>

#include "Log/SpdLogSystem.h"

namespace Spark
{
    enum class EBusAddressPolicy
    {
        Single,
        ById,
        ByIdAndOrdered,
    };

    enum class EBusHandlerPolicy
    {
        Single,
        Multiple,
        MultipleAndOrdered,
    };

    struct NullBusMessageCall
    {
        template<typename Function>
        NullBusMessageCall(Function) {}
        template<typename Function, typename Allocator>
        NullBusMessageCall(Function, const Allocator&) {}
    };

    struct BusHandlerCompareDefault;

    template <typename Context>
    struct EBusGlobalStoragePolicy
    {
        static Context* Get()
        {
            return &GetOrCreate();
        }

        static Context& GetOrCreate()
        {
            static Context s_context;
            return s_context;
        }
    };

    template <typename Context>
    struct EBusThreadLocalStoragePolicy
    {
        static Context* Get()
        {
            return &GetOrCreate();
        }

        static Context& GetOrCreate()
        {
            thread_local static Context s_context;
            return s_context;
        }
    };

    template <bool IsEnabled, typename Bus, typename MutexType>
    struct EBusQueuePolicy
    {
        using BusMessageCall = NullBusMessageCall;
        void Execute() {};
        void Clear() {};
        void SetActive(bool /*isActive*/) {};
        bool IsActive() { return false; }
        size_t Count() const { return 0; }
    };

    template <typename Bus, typename MutexType>
    struct EBusQueuePolicy<true, Bus, MutexType>
    {
        using BusMessageCall = eastl::function<void()>;
       
        using DequeType = eastl::deque<BusMessageCall, typename Bus::AllocatorType>;
        using MessageQueueType = eastl::queue<BusMessageCall, DequeType>;

        EBusQueuePolicy() = default;

        bool                 m_isActive = Bus::Traits::EventQueueingActiveByDefault;
        MessageQueueType     m_messages;
        MutexType            m_messagesMutex; 

        void Execute()
        {
            if (!m_isActive)
            {
                LOG_WARN("[EBus] You are calling execute queued functions on a bus which has not activated its function queuing!");
            }

            MessageQueueType localMessages;

            // Swap the current list of queue functions with a local instance
            {
                std::unique_lock lock(m_messagesMutex);
                eastl::swap(localMessages, m_messages);
            }

            // Execute the queue functions safely now that are owned by the function
            while (!localMessages.empty())
            {
                const BusMessageCall& localMessage = localMessages.front();
                localMessage();
                localMessages.pop();
            }
        }

        void Clear()
        {
            std::lock_guard<MutexType> lock(m_messagesMutex);
            m_messages = {};
        }

        void SetActive(bool isActive)
        {
            std::lock_guard<MutexType> lock(m_messagesMutex);
            m_isActive = isActive;
            if (!m_isActive)
            {
                m_messages = {};
            }
        };

        bool IsActive()
        {
            return m_isActive;
        }

        size_t Count()
        {
            std::lock_guard<MutexType> lock(m_messagesMutex);
            return m_messages.size();
        }
    };

    struct EBusEventProcessingPolicy
    {
        template<typename Results, typename Function, typename Interface, typename... InputArgs>
        static void CallResult(Results& results, Function&& func, Interface&& intf, InputArgs&&... args)
        {
            results = eastl::invoke(eastl::forward<Function>(func), eastl::forward<Interface>(intf), eastl::forward<InputArgs>(args)...);
        }

        template<typename Function, typename Interface, typename... InputArgs>
        static void Call(Function&& func, Interface&& intf, InputArgs&&... args)
        {
            eastl::invoke(eastl::forward<Function>(func), eastl::forward<Interface>(intf), eastl::forward<InputArgs>(args)...);
        }
    };
}