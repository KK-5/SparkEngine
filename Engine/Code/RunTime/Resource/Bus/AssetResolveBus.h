#pragma once

#include <mutex>
#include <EBus/EBus.h>
#include <Base.h>

namespace Spark::Resource
{
    class ModelAsset;

    struct AssetResolveBusTraits : public EBusTraits
    {
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::Single;

        static constexpr bool EnableEventQueue = true;

        using MutexType = std::mutex;

        virtual void ResolveModelAssetToScene(Ptr<ModelAsset> asset) {}

    };

    using AssetResolveBus = EBus<AssetResolveBusTraits>;
}