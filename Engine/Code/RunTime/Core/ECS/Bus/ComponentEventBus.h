#pragma once

#include <EBUS/EBus.h>
#include <Reflection/RTTI.h>

#include "../Entity.h"

namespace Spark
{
    class WorldContext;

    class ComponentEvent : public EBusTraits
    {
    public:
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ById;

        using BusIdType = TypeId;
    
    public:
        virtual void OnComponentConstruct(WorldContext& context, Entity entity) {};

        virtual void OnComponentUpdate(WorldContext& context, Entity entity) {};

        virtual void OnComponentDestory(WorldContext& context, Entity entity) {};
    };

    using ComponentEventBus = EBus<ComponentEvent>;

}