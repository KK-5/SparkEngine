#include "ComponentAssetResolver.h"

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <Reflection/RTTI.h>
#include <Reflection/TypeRegistry.h>
#include <HashString/HashString.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Asset.h>
#include <Resource/Model/ModelAsset.h>

namespace Editor
{
    using namespace Spark;

    ComponentAssetResolver::ComponentAssetResolver()
    {
        Resource::AssetResolveBus::Handler::BusConnect();
    }

    ComponentAssetResolver::~ComponentAssetResolver()
    {
        if (BusIsConnected())
        {
            BusDisconnect();
        }
    }

    void ComponentAssetResolver::ResolveAssetToComponent(
        Entity                     entity,
        TypeId                     componentType,
        TypeId                     fieldId,
        Resource::AssetId          assetId,
        Resource::AssetType        assetType)
    {
        // Enforce the invariant: a component only ever holds a loaded asset id. By
        // the time this fires the asset should be Ready (AssetHandler queued it on
        // Ready); bail rather than write a not-ready id if that ever slips.
        if (auto* am = Service<Resource::AssetManager>::Get())
        {
            Ptr<Resource::Asset> asset = am->FindAsset(assetId);
            if (!asset || asset->GetStatus() != Resource::AssetStatus::Ready)
            {
                LOG_WARN("[ComponentAssetResolver] Asset '{}' not ready at resolve time; skipping.",
                         assetId.GetPath().c_str());
                return;
            }
            if (asset->GetAssetType() != assetType)
            {
                LOG_WARN("[ComponentAssetResolver] Asset '{}' type mismatch; skipping.",
                         assetId.GetPath().c_str());
                return;
            }
        }

        auto* world = WorldExecuteContext::Current();
        if (!world)
        {
            LOG_ERROR("[ComponentAssetResolver] No WorldContext is active.");
            return;
        }

        MetaType component = TypeRegistry::GetContext().Resolve(componentType);
        if (!component)
        {
            LOG_ERROR("[ComponentAssetResolver] Component type {} is not reflected.", componentType);
            return;
        }

        // The entity may have lost the component (deleted) during the async gap.
        MetaAny hasAny = component.func("HasComponent"_hs).invoke({}, AnyCast(*world), entity);
        if (!hasAny || !hasAny.cast<bool>())
        {
            LOG_WARN("[ComponentAssetResolver] Entity no longer has component {}; skipping resolve.",
                     componentType);
            return;
        }

        MetaData field = component.data(fieldId);
        if (!field)
        {
            LOG_ERROR("[ComponentAssetResolver] Field {} not found on component {}.", fieldId, componentType);
            return;
        }

        // Re-fetch the live component now (the instance was deliberately not stored),
        // write the id, then ReplaceComponent to fire OnComponentUpdated so the owning
        // system re-resolves and rebuilds its GPU resources.
        MetaAny instancePtr = component.func("GetComponent"_hs).invoke({}, AnyCast(*world), entity);
        if (!instancePtr)
        {
            LOG_ERROR("[ComponentAssetResolver] GetComponent failed for component {}.", componentType);
            return;
        }

        MetaAny instance = *instancePtr;
        field.set(instance, assetId);
        component.func("ReplaceComponent"_hs).invoke({}, AnyCast(*world), entity, instance);

        LOG_INFO("[ComponentAssetResolver] Resolved asset '{}' into component {}.",
                 assetId.GetPath().c_str(), componentType);
    }
}
