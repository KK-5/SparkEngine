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
#include <Material/MaterialUtils.h>

namespace Editor
{
    using namespace Spark;

    namespace
    {
        //! Enforce the invariant: a component only ever holds a loaded asset. By the time
        //! a resolve fires the asset should be Ready (AssetHandler queues it on Ready);
        //! bail rather than write a not-ready id if that ever slips.
        bool AssetUsable(const Resource::AssetId& assetId, Resource::AssetType expected)
        {
            auto* am = Service<Resource::AssetManager>::Get();
            if (!am)
            {
                return true;
            }

            Ptr<Resource::Asset> asset = am->FindAsset(assetId);
            if (!asset || asset->GetStatus() != Resource::AssetStatus::Ready)
            {
                LOG_WARN("[ComponentAssetResolver] Asset '{}' not ready at resolve time; skipping.",
                         assetId.GetPath().c_str());
                return false;
            }
            if (asset->GetAssetType() != expected)
            {
                LOG_WARN("[ComponentAssetResolver] Asset '{}' type mismatch; skipping.",
                         assetId.GetPath().c_str());
                return false;
            }
            return true;
        }

        //! The write step both terminals share. The component instance is deliberately
        //! re-fetched here rather than carried: the entity may have lost the component
        //! during the async gap, and a stored instance would dangle.
        bool WriteComponentField(Entity entity, TypeId componentType, TypeId fieldId, MetaAny value)
        {
            MetaType component = TypeRegistry::GetContext().Resolve(componentType);
            if (!component)
            {
                LOG_ERROR("[ComponentAssetResolver] Component type {} is not reflected.", componentType);
                return false;
            }

            const uint32_t entityId = static_cast<uint32_t>(entity);

            MetaAny hasAny = component.func("HasComponent"_hs).invoke({}, entityId);
            if (!hasAny || !hasAny.cast<bool>())
            {
                LOG_WARN("[ComponentAssetResolver] Entity no longer has component {}; skipping resolve.",
                         componentType);
                return false;
            }

            MetaData field = component.data(fieldId);
            if (!field)
            {
                LOG_ERROR("[ComponentAssetResolver] Field {} not found on component {}.", fieldId, componentType);
                return false;
            }

            MetaAny instancePtr = component.func("GetComponent"_hs).invoke({}, entityId);
            if (!instancePtr)
            {
                LOG_ERROR("[ComponentAssetResolver] GetComponent failed for component {}.", componentType);
                return false;
            }

            MetaAny instance = *instancePtr;
            if (!field.set(instance, value))
            {
                LOG_ERROR("[ComponentAssetResolver] Field {} on component {} rejected the value.",
                          fieldId, componentType);
                return false;
            }

            // ReplaceComponent, not just the set above, so OnComponentUpdated fires and the
            // owning system re-resolves.
            component.func("ReplaceComponent"_hs).invoke({}, entityId, instance);
            return true;
        }
    }

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
        if (!AssetUsable(assetId, assetType))
        {
            return;
        }

        if (WriteComponentField(entity, componentType, fieldId, assetId))
        {
            LOG_INFO("[ComponentAssetResolver] Resolved asset '{}' into component {}.",
                     assetId.GetPath().c_str(), componentType);
        }
    }

    void ComponentAssetResolver::ResolveMaterialToComponent(
        Entity            entity,
        TypeId            componentType,
        TypeId            fieldId,
        Resource::AssetId assetId)
    {
        if (!AssetUsable(assetId, Resource::AssetType::Material))
        {
            return;
        }

        // The preprocessing step this terminal exists for: the field holds a material
        // entity, not an id. One material entity per asset, so dropping the same `.smat`
        // on a second object shares the first one's material.
        const Material::MaterialHandle handle = Material::MaterialFromAssetId(assetId);
        if (handle == Material::NullMaterial)
        {
            LOG_WARN("[ComponentAssetResolver] Material '{}' could not be resolved; slot left unchanged.",
                     assetId.GetPath().c_str());
            return;
        }

        if (WriteComponentField(entity, componentType, fieldId, handle))
        {
            LOG_INFO("[ComponentAssetResolver] Assigned material '{}' to component {}.",
                     assetId.GetPath().c_str(), componentType);
        }
    }
}
