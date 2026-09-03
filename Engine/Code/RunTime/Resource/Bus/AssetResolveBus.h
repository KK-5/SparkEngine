#pragma once

#include <mutex>
#include <EBus/EBus.h>
#include <Base.h>

#include <ECS/Common.h>
#include <Reflection/RTTI.h>
#include <Resource/AssetTypes.h>

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

        //! Write a now-ready asset id into a component field on the main thread.
        //! Queued (never broadcast directly) from AssetHandler's OnAssetReady, which
        //! runs on the AssetManager worker thread; the handler (ComponentAssetResolver)
        //! fires during ExecuteQueuedEvents() so it may touch ECS / reflection safely.
        //! Type-agnostic: the resolver resolves componentType, fetches the live
        //! component, sets fieldId to assetId, then ReplaceComponent → OnComponentUpdated.
        virtual void ResolveAssetToComponent(
            Entity    entity,
            TypeId    componentType,
            TypeId    fieldId,
            AssetId   assetId,
            AssetType assetType) {}

        //! Same, for a field holding a material reference rather than an asset id: the id
        //! is turned into a material entity first, and that handle is what gets written.
        //!
        //! A sibling event rather than a branch inside the one above, because the two
        //! differ in a preprocessing step, and only the drop site knows which kind of
        //! field it targeted. No assetType parameter -- it is Material by construction.
        virtual void ResolveMaterialToComponent(
            Entity  entity,
            TypeId  componentType,
            TypeId  fieldId,
            AssetId assetId) {}
    };

    using AssetResolveBus = EBus<AssetResolveBusTraits>;
}