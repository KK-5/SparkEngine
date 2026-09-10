#pragma once

#include <EASTL/span.h>

#include <EBUS/EBus.h>
#include <Reflection/RTTI.h>

#include "../Common.h"

namespace Spark
{

    class ComponentEvents : public EBusTraits
    {
    public:
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ById;

        using BusIdType = TypeId;
    
    public:
        virtual void OnComponentConstruct(Entity entity) {};

        /// Dispatched when a batch of this component appears at once. The components in the batch,
        /// and the references between them, are already consistent -- reconcile derived state, do
        /// not link each one in as if it had arrived alone.
        ///
        /// The span is only valid for the duration of the call, so this event cannot be queued.
        virtual void OnComponentsConstruct(eastl::span<const Entity> entities)
        {
            for (Entity entity : entities)
            {
                OnComponentConstruct(entity);
            }
        }

        /// Dispatched from WorldContext::Replace before registry state is replaced when ComponentEventMask::WillUpdate is set.
        virtual void OnComponentWillUpdate(Entity entity) {};

        /// Dispatched from WorldContext::Replace after registry state is replaced when ComponentEventMask::Updated is set.
        virtual void OnComponentUpdated(Entity entity) {};

        virtual void OnComponentDestory(Entity entity) {};
    };

    using ComponentEventBus = EBus<ComponentEvents>;

}