#pragma once

#include <EASTL/optional.h>

#include <EBus/EBus.h>

#include "../Entity.h"

namespace Spark
{
    class EntityEvent : public EBusTraits
    {
    public:
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::Single;

        static constexpr bool EnableEventQueue = true;

    public:
        virtual void OnEntityCreate(Entity entity) = 0;

        virtual void OnEntityDestory(Entity entity) = 0;
    };

    using EntityEventBus = EBus<EntityEvent>;
}