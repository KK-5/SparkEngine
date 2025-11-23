#pragma once

#include <EASTL/functional.h>
#include <EASTL/type_traits.h>
#include <mutex>

#include <Log/SpdLogSystem.h>

#include "Polices.h"
#include "Container.h"
#include "CallstackEntry.h"

namespace Spark
{
    struct EBusNullQueue
    {
    };

    /* O3DE中用来检验QueueEvent中传入的函数参数是否为const &类型，eastl和std中暂时没有这些tarits，所以没有使用，
       考虑到使用ECS经常需要传入world context引用，这个功能可以忽略

    template <class Function, bool AllowQueuedReferences>
    struct QueueFunctionArgumentValidator
    {
        static void Validate() {}
    };

    template <class Function>
    struct ArgumentValidatorHelper
    {
        template<typename T>
        using is_non_const_lvalue_reference = eastl::integral_constant<bool, eastl::is_lvalue_reference<T>::value && !eastl::is_const<eastl::remove_reference_t<T>>::value>;

        template <size_t... ArgIndices>
        constexpr static void ValidateHelper(eastl::index_sequence<ArgIndices...>)
        {
            std::function_traits_get_arg_t;
            static_assert(!eastl::disjunction_v<is_non_const_lvalue_reference<eastl::function_traits_get_arg_t<Function, ArgIndices>>...>,
                "It is not safe to queue a function call with non-const lvalue ref arguments");
        }

        constexpr static void Validate()
        {
            ValidateHelper(eastl::make_index_sequence<eastl::function_traits<Function>::num_args>());
        }
    };
    
    template <class Function>
    struct QueueFunctionArgumentValidator<Function, false>
    {
        using Validator = ArgumentValidatorHelper<Function>;
        constexpr static void Validate()
        {
            Validator::Validate();
        }
    };
    */

    template <typename EBus, typename Traits>
    struct EBusBroadcastQueue
    {
        using Bus = EBus;

        inline static void ExecuteQueuedEvents()
        {
            if (auto* context = Bus::GetContext())
            {
                context->m_queue.Execute();
            }
        }

        inline static void ClearQueuedEvents()
        {
            if (auto* context = Bus::GetContext(false))
            {
                context->m_queue.Clear();
            }
        }

        inline static size_t QueuedEventCount()
        {
            if (auto* context = Bus::GetContext(false))
            {
                return context->m_queue.Count();
            }
            return 0;
        }

        inline static void AllowFunctionQueuing(bool isAllowed) { Bus::GetOrCreateContext().m_queue.SetActive(isAllowed); }

        inline static bool IsFunctionQueuing()
        {
            auto* context = Bus::GetContext();
            return context ? context->m_queue.IsActive() : Traits::EventQueueingActiveByDefault;
        }

        template <typename Function, typename ... InputArgs>
        static void QueueFunction(Function&& func, InputArgs&& ... args)
        {
            static_assert((eastl::is_same<typename Bus::QueuePolicy::BusMessageCall, NullBusMessageCall>::value == false),
                "This EBus doesn't support queued events! Check 'EnableEventQueue'");

            auto& context = Bus::GetOrCreateContext(false);
            if (context.m_queue.IsActive())
            {
                std::scoped_lock<decltype(context.m_queue.m_messagesMutex)> messageLock(context.m_queue.m_messagesMutex);
                context.m_queue.m_messages.push(typename Bus::QueuePolicy::BusMessageCall(
                    [func = eastl::forward<Function>(func), args...]() mutable
                    {
                        eastl::invoke(eastl::forward<Function>(func), eastl::forward<InputArgs>(args)...);
                    }
                    )
                );
            }
            else
            {
                LOG_WARN("[EBus] Unable to queue function onto EBus.  This may be due to a previous call to AllowFunctionQueuing(false)."
                    "  Hint: This is often disabled during shutdown of a ComponentApplication");
            }
        }

        template <typename Function, typename ... InputArgs>
        inline static void QueueBroadcast(Function&& func, InputArgs&& ... args)
        {
            using Broadcaster = void(*)(Function&&, InputArgs&&...);
            Bus::QueueFunction(static_cast<Broadcaster>(&Bus::Broadcast), eastl::forward<Function>(func), eastl::forward<InputArgs>(args)...);
        }

        template <typename Function, typename ... InputArgs>
        inline static void TryQueueBroadcast(Function&& func, InputArgs&& ... args)
        {
            if (IsFunctionQueuing())
            {
                QueueBroadcast(eastl::forward<Function>(func), eastl::forward<InputArgs>(args)...);
            }
        }
    };

    template <typename EBus, typename Traits>
    struct EBusEventQueue: public EBusBroadcastQueue<EBus, Traits>
    {
        using Bus = EBus;
        using BusIdType = typename Traits::BusIdType;
        using BusPtr = typename Traits::BusPtr;

        template <typename Function, typename ... InputArgs>
        inline static void QueueEvent(const BusIdType& id, Function&& func, InputArgs&& ... args)
        {
            using Eventer = void(*)(const BusIdType&, Function&&, InputArgs&&...);
            QueueFunction(static_cast<Eventer>(&Bus::Event), id, eastl::forward<Function>(func), eastl::forward<InputArgs>(args)...);
        }

        template <typename Function, typename ... InputArgs>
        inline void TryQueueEvent(const BusIdType& id, Function&& func, InputArgs&& ... args)
        {
            if (EBusBroadcastQueue<EBus, Traits>::IsFunctionQueuing())
            {
                QueueEvent(id, eastl::forward<Function>(func), eastl::forward<InputArgs>(args)...);
            }
        }

        template <typename Function, typename ... InputArgs>
        inline void QueueEvent(const BusPtr& ptr, Function&& func, InputArgs&& ... args)
        {
            using Eventer = void(*)(const BusPtr&, Function&&, InputArgs&&...);
           QueueFunction(static_cast<Eventer>(&Bus::Event), ptr, eastl::forward<Function>(func), eastl::forward<InputArgs>(args)...);
        }

        template <typename Function, typename ... InputArgs>
        inline void TryQueueEvent(const BusPtr& ptr, Function&& func, InputArgs&& ... args)
        {
            if (EBusBroadcastQueue<EBus, Traits>::IsFunctionQueuing())
            {
                QueueEvent(ptr, eastl::forward<Function>(func), eastl::forward<InputArgs>(args)...);
            }
        }
    };
}

