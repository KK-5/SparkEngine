#include "AssetHandler.h"

#include <EASTL/algorithm.h>

#include <ECS/ExecuteContext.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Model/ModelAsset.h>
#include <Resource/Asset.h>
#include <Spawn/SpawnModel.h>
#include <Resource/Bus/AssetResolveBus.h>

namespace Editor
{
    AssetHandler::AssetHandler()
    {
        using namespace Spark;

        AssetEditBus::Handler::BusConnect();

        // Only the two types a component field can still be waiting on. A texture slot
        // re-tags the id it was handed with its own usage, so that identity was never
        // preloaded -- see OnAssetDragToComponent.
        Resource::AssetBus::MultiHandler::BusConnect(Resource::AssetType::Image);
        Resource::AssetBus::MultiHandler::BusConnect(Resource::AssetType::Material);
    }

    AssetHandler::~AssetHandler()
    {
        AssetEditBus::Handler::BusDisconnect();
        Spark::Resource::AssetBus::MultiHandler::BusDisconnect();
    }

    void AssetHandler::OnModelAssetDragToScene(const Spark::Resource::ModelAsset& asset)
    {
        using namespace Spark;

        auto* worldCtx = WorldExecuteContext::Current();
        if (!worldCtx)
        {
            LOG_ERROR("[AssetHandler] No WorldContext is active.");
            return;
        }

        // A model in the browser was registered, and a registered asset is preloaded or
        // requested the moment it appears -- so there is nothing to wait for here.
        if (!asset.IsReady())
        {
            LOG_ERROR("[AssetHandler] Model asset '{}' is not ready; nothing spawned.",
                      asset.GetAssetId().GetPath().c_str());
            return;
        }

        Ptr<Resource::ModelAsset> model(const_cast<Resource::ModelAsset*>(&asset));
        Spawn::SpawnModel(model, *worldCtx);
    }

    void AssetHandler::OnAssetDragToComponent(
        Spark::Entity              entity,
        Spark::TypeId              componentType,
        Spark::TypeId              fieldId,
        Spark::Resource::AssetId   assetId,
        Spark::Resource::AssetType assetType)
    {
        QueueComponentBind(
            PendingComponentBind{ assetId, entity, componentType, fieldId, assetType, BindKind::AssetId });
    }

    void AssetHandler::OnMaterialDragToComponent(
        Spark::Entity            entity,
        Spark::TypeId            componentType,
        Spark::TypeId            fieldId,
        Spark::Resource::AssetId assetId)
    {
        QueueComponentBind(PendingComponentBind{
            assetId, entity, componentType, fieldId, Spark::Resource::AssetType::Material,
            BindKind::Material });
    }

    void AssetHandler::QueueComponentBind(PendingComponentBind bind)
    {
        using namespace Spark;

        const Resource::AssetId assetId = bind.assetId;
        if (!assetId.IsValid())
        {
            return;
        }

        // Record identity first so the bind is matched whether the asset is already
        // ready, still loading, or has to be requested fresh.
        m_pendingBinds.push_back(bind);

        auto* am = Service<Resource::AssetManager>::Get();
        if (!am)
        {
            LOG_ERROR("[AssetHandler] AssetManager service is unavailable.");
            return;
        }

        Ptr<Resource::Asset> asset = am->FindAsset(assetId);
        if (asset && asset->GetStatus() == Resource::AssetStatus::Ready)
        {
            // Already loaded — no Ready event will come; resolve immediately (queued
            // to the main thread, so it still lands during ExecuteQueuedEvents()).
            LOG_INFO("[AssetHandler] Asset '{}' already ready, queuing component resolve.",
                     assetId.GetPath().c_str());
            ResolvePendingBinds(assetId);
            return;
        }

        LOG_INFO("[AssetHandler] Requesting async load for asset '{}' (component field).",
                 assetId.GetPath().c_str());
        Ptr<Resource::Asset> requested = am->RequestAsset(assetId);
        if (!requested)
        {
            LOG_ERROR("[AssetHandler] Failed to request asset '{}'.", assetId.GetPath().c_str());
            DropPendingBinds(assetId); // request failed; nothing will arrive, discard the bind
        }
    }

    void AssetHandler::OnAssetReady(Spark::Resource::Asset& asset)
    {
        ResolvePendingBinds(asset.GetAssetId());
    }

    void AssetHandler::OnAssetError(Spark::Resource::Asset& asset)
    {
        DropPendingBinds(asset.GetAssetId());
    }

    void AssetHandler::ResolvePendingBinds(const Spark::Resource::AssetId& assetId)
    {
        using namespace Spark;

        for (auto it = m_pendingBinds.begin(); it != m_pendingBinds.end();)
        {
            if (it->assetId == assetId)
            {
                if (it->kind == BindKind::Material)
                {
                    Resource::AssetResolveBus::QueueBroadcast(
                        &Resource::AssetResolveBusTraits::ResolveMaterialToComponent,
                        it->entity, it->componentType, it->fieldId, it->assetId);
                }
                else
                {
                    Resource::AssetResolveBus::QueueBroadcast(
                        &Resource::AssetResolveBusTraits::ResolveAssetToComponent,
                        it->entity, it->componentType, it->fieldId, it->assetId, it->assetType);
                }
                it = m_pendingBinds.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void AssetHandler::DropPendingBinds(const Spark::Resource::AssetId& assetId)
    {
        using namespace Spark;

        const size_t before = m_pendingBinds.size();
        m_pendingBinds.erase(
            eastl::remove_if(m_pendingBinds.begin(), m_pendingBinds.end(),
                [&](const PendingComponentBind& bind) { return bind.assetId == assetId; }),
            m_pendingBinds.end());
        if (m_pendingBinds.size() != before)
        {
            LOG_ERROR("[AssetHandler] Asset '{}' failed to load; dropped component binds.",
                      assetId.GetPath().c_str());
        }
    }
}
