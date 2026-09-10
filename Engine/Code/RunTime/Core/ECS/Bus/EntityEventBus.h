#pragma once

#include <EASTL/optional.h>
#include <EASTL/span.h>

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

        /// Dispatched when a batch of entities appears at once.
        /// The span is only valid for the duration of the call, so this event cannot be queued.
        virtual void OnEntitiesCreate(eastl::span<const Entity> entities)
        {
            for (Entity entity : entities)
            {
                OnEntityCreate(entity);
            }
        }

        virtual void OnEntityDestory(Entity entity) = 0;
    };

    using EntityEventBus = EBus<EntityEvent>;
}