#pragma once

#include <EBUS/EBus.h>
#include <Reflection/RTTI.h>

#include "../Entity.h"

namespace Spark
{
    class WorldContext;

    class ComponentEvents : public EBusTraits
    {
    public:
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ById;

        using BusIdType = TypeId;
    
    public:
        virtual void OnComponentConstruct(WorldContext& context, Entity entity) {};

        /// Dispatched from WorldContext::Repalce before registry state is replaced when ComponentEventMask::WillUpdate is set.
        virtual void OnComponentWillUpdate(WorldContext& context, Entity entity) {};

        /// Dispatched from WorldContext::Repalce after registry state is replaced when ComponentEventMask::Updated is set.
        virtual void OnComponentUpdated(WorldContext& context, Entity entity) {};

        virtual void OnComponentDestory(WorldContext& context, Entity entity) {};
    };

    using ComponentEventBus = EBus<ComponentEvents>;

}