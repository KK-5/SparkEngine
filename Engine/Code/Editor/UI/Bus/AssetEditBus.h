#pragma once

#include <EBus/EBus.h>

#include <Resource/AssetTypes.h>

namespace Editor
{
    struct AssetEditEvents : public Spark::EBusTraits
    {
        static const Spark::EBusHandlerPolicy HandlerPolicy = Spark::EBusHandlerPolicy::Multiple;
        static const Spark::EBusAddressPolicy AddressPolicy = Spark::EBusAddressPolicy::Single;

        virtual void OnAssetDragBegin(Spark::Resource::AssetId id, Spark::Resource::AssetType type,
                                       float screenX, float screenY) {}
        virtual void OnAssetDragEnd(Spark::Resource::AssetId id, Spark::Resource::AssetType type,
                                     float screenX, float screenY, bool accepted) {}
    };

    using AssetEditBus = Spark::EBus<AssetEditEvents>;
}
