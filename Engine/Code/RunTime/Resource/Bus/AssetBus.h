#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <EBus/EBus.h>

#include <Resource/AssetTypes.h>

namespace Spark::Resource
{
    class Asset;

    // NOTE: AssetBus events are dispatched from the AssetManager worker thread
    // (see AssetManager::ProcessThread). Handlers MUST NOT perform write
    // operations on ECS contexts (WorldContext, RHIContext, etc.) — reading is
    // unsafe too unless the owning system has explicit synchronization in place.
    // To mutate ECS state in response to asset events, use AssetResolveBus
    // instead, which has EnableEventQueue = true and fires handlers on the main
    // thread after ExecuteQueuedEvents().
    struct AssetBusTraits : public EBusTraits
    {
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ById;

        using BusIdType = AssetType;

        // These fire on the AssetManager worker thread.
        virtual void OnAssetReady(Asset& asset) {}
        virtual void OnAssetError(Asset& asset) {}

    };

    using AssetBus = EBus<AssetBusTraits>;
}