#pragma once

#include <EBus/EBus.h>

#include <Base.h>

#include <Resource/AssetTypes.h>


namespace Spark::Resource
{
    class Asset;
    class AssetBuildContext;

    struct AssetBuildEvents : public EBusTraits
    {
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Single;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ById;

        using BusIdType = AssetType;

        virtual Ptr<Asset> CreateAsset(const AssetId& id) = 0;
        virtual void       Load(AssetBuildContext& ctx) = 0;
        virtual void       Compile(AssetBuildContext& ctx) = 0;
    };

    using AssetBuildBus = EBus<AssetBuildEvents>;
}
