#pragma once

#include <Resource/Bus/AssetResolveBus.h>

namespace Editor
{
    //! Main-thread resolver that writes a now-ready asset id into a component field.
    //!
    //! Handles AssetResolveBus::ResolveAssetToComponent, which AssetHandler queues
    //! once an asset finishes loading. Type-agnostic by reflection: it resolves the
    //! component type, fetches the live component on the active world, sets the
    //! target field to the asset id, then ReplaceComponent → OnComponentUpdated so
    //! the owning system (e.g. SkyboxSystem) re-resolves and rebuilds GPU resources.
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
    };
}
