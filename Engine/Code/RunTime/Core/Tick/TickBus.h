#pragma once 

#include <mutex>

#include <EBus/EBus.h>
#include <ECS/WorldContext.h>

#include "TickOrder.h"

namespace Spark
{
    class TickEvents: public EBusTraits
    {
    public:
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::MultipleAndOrdered;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::Single;
        
        // 允许异步执行和递归Dispatch/Connect
        // static const bool EnableEventQueue = true;
        // using EventQueueMutexType = std::recursive_mutex;

        inline bool Compare(const TickEvents* other) const
        {
            return GetTickOrder() < other->GetTickOrder();
        }
    public:
        TickEvents() = default;
        virtual ~TickEvents() = default;
        
        virtual void         OnTick(WorldContext& context, float deltaTime) = 0;
        virtual unsigned int GetTickOrder() const
        {
            return static_cast<unsigned int>(TickOrder::TICK_DEFAULT);
        }
    };

    using TickBus = EBus<TickEvents>;
}