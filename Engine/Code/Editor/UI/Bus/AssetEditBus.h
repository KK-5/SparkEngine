#pragma once

#include <EBus/EBus.h>

#include <ECS/Common.h>
#include <Reflection/RTTI.h>
#include <Resource/AssetTypes.h>

namespace Spark::Resource
{
    class ModelAsset;
}

namespace Editor
{
    struct AssetEditEvents : public Spark::EBusTraits
    {
        static const Spark::EBusHandlerPolicy HandlerPolicy = Spark::EBusHandlerPolicy::Multiple;
        static const Spark::EBusAddressPolicy AddressPolicy = Spark::EBusAddressPolicy::Single;

        virtual void OnAssetDragBegin(
            Spark::Resource::AssetId   id,
            Spark::Resource::AssetType type,
            float                      screenX,
            float                      screenY) {}

        virtual void OnAssetDragEnd(
            Spark::Resource::AssetId   id,
            Spark::Resource::AssetType type,
            float                      screenX,
            float                      screenY,
            bool                       accepted) {}

        virtual void OnModelAssetDragToScene(const Spark::Resource::ModelAsset& asset) {}

        //! An asset was dropped onto an editable AssetElement field of a component.
        //! The UI only carries identity here — it does NOT load or write the field.
        //! AssetHandler takes it from here: async-load the asset, then (once ready)
        //! fire AssetResolveBus::ResolveAssetToComponent on the main thread, which
        //! writes the id into the live component via reflection. See the P0 flow in
        //! TODO_SkyboxCubemapPlan.md §2.5.
        //!  - componentType : GetTypeId<Component>() of the owning component
        //!  - fieldId       : reflected MetaData id of the target AssetElement field
        virtual void OnAssetDragToComponent(
            Spark::Entity              entity,
            Spark::TypeId              componentType,
            Spark::TypeId              fieldId,
            Spark::Resource::AssetId   assetId,
            Spark::Resource::AssetType assetType) {}
    };

    using AssetEditBus = Spark::EBus<AssetEditEvents>;
}
