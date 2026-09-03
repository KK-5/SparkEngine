#pragma once

#include <Resource/Bus/AssetResolveBus.h>

namespace Editor
{
    //! Main-thread resolver that writes a now-ready asset into a component field.
    //!
    //! Handles both terminals AssetHandler queues once an asset finishes loading. They
    //! share one write step -- resolve the component type, fetch the live component on
    //! the active world, set the field, then ReplaceComponent → OnComponentUpdated so the
    //! owning system (e.g. SkyboxSystem) re-resolves and rebuilds GPU resources -- and
    //! differ only in what value goes in: the asset id itself, or the material entity
    //! that id resolves to.
    class ComponentAssetResolver final : public Spark::Resource::AssetResolveBus::Handler
    {
    public:
        ComponentAssetResolver();
        ~ComponentAssetResolver() override;

        void ResolveAssetToComponent(
            Spark::Entity              entity,
            Spark::TypeId              componentType,
            Spark::TypeId              fieldId,
            Spark::Resource::AssetId   assetId,
            Spark::Resource::AssetType assetType) override;

        void ResolveMaterialToComponent(
            Spark::Entity            entity,
            Spark::TypeId            componentType,
            Spark::TypeId            fieldId,
            Spark::Resource::AssetId assetId) override;
    };
}
