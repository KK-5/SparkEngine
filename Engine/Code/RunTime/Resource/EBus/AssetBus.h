#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <EBus/EBus.h>

#include <Resource/AssetTypes.h>

namespace Spark::Resource
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

    struct AssetCatalogBusTraits : public EBusTraits
    {
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::Single;

        // Interface
        virtual void OnAssetSearchPathsChange(const eastl::vector<eastl::string>& paths) = 0;
    };

    using AssetCatalogBus = EBus<AssetCatalogBusTraits>;
}