#pragma once

#include <EBus/EBus.h>

#include <Resource/AssetTypes.h>

namespace Spark::Asset
{
    class Asset;

    struct AssetBusTraits : public EBusTraits
    {
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ById;

        using BusIdType = AssetType;

        /// Interface
        virtual void OnAssetReady(Asset& asset) {}
        virtual void OnAssetError(Asset& asset) {}

    };

    using AssetBus = EBus<AssetBusTraits>;
}