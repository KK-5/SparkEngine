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

        // MultiHandler: observe Ready/Error for every asset type a component field
        // might accept. Model is needed for drag-to-scene; Image for skybox / textures;
        // Material for the material slot.
        //
        // This subscription exists only to wait, so it goes away together with the pending
        // tracks below once asset preloading guarantees a dropped asset is already Ready.
        Resource::AssetBus::MultiHandler::BusConnect(Resource::AssetType::Model);
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

        if (asset.IsReady())
        {
            LOG_INFO("[AssetHandler] Model asset '{}' is ready, extracting to world.", asset.GetName().GetCStr());
            Ptr<Resource::ModelAsset> model(const_cast<Resource::ModelAsset*>(&asset));
            Spawn::SpawnModel(model, *worldCtx);
            return;
        }

        const Resource::AssetId& assetId = asset.GetAssetId();
        if (eastl::find(m_loadingAssets.begin(), m_loadingAssets.end(), assetId) != m_loadingAssets.end())
        {
            LOG_INFO("[AssetHandler] Model asset '{}' is already queued for loading.", asset.GetName().GetCStr());
            return;
        }

        if (asset.IsLoading())
        {
            LOG_INFO("[AssetHandler] Model asset '{}' is loading, tracking for completion.", asset.GetName().GetCStr());
            m_loadingAssets.push_back(assetId);
            return;
        }

        auto* am = Service<Resource::AssetManager>::Get();
        if (!am)
        {
            LOG_ERROR("[AssetHandler] AssetManager service is unavailable.");
            return;
        }

        LOG_INFO("[AssetHandler] Requesting async load for model asset '{}'.", asset.GetName().GetCStr());
        Ptr<Resource::Asset> requested = am->RequestAsset(assetId);
        if (requested)
        {
            m_loadingAssets.push_back(assetId);
        }
        else
        {
            LOG_ERROR("[AssetHandler] Failed to request model asset '{}'.", asset.GetName().GetCStr());
        }
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
        // Fan out to every track; each ignores assets it isn't waiting on.
        ResolvePendingScene(asset);
        ResolvePendingBinds(asset.GetAssetId());
    }

    void AssetHandler::OnAssetError(Spark::Resource::Asset& asset)
    {
        const Spark::Resource::AssetId& assetId = asset.GetAssetId();
        DropPendingScene(assetId);
        DropPendingBinds(assetId);
    }

    void AssetHandler::ResolvePendingScene(Spark::Resource::Asset& asset)
    {
        using namespace Spark;

        const Resource::AssetId& assetId = asset.GetAssetId();
        auto it = eastl::find(m_loadingAssets.begin(), m_loadingAssets.end(), assetId);
        if (it == m_loadingAssets.end())
        {
            return;
        }

        LOG_INFO("[AssetHandler] Model asset '{}' ready, queuing scene resolve.", asset.GetName().GetCStr());
        Ptr<Resource::ModelAsset> model(static_cast<Resource::ModelAsset*>(&asset));
        Resource::AssetResolveBus::QueueBroadcast(
            &Resource::AssetResolveBusTraits::ResolveModelAssetToScene, eastl::move(model));
        m_loadingAssets.erase(it);
    }

    void AssetHandler::DropPendingScene(const Spark::Resource::AssetId& assetId)
    {
        using namespace Spark;

        auto it = eastl::find(m_loadingAssets.begin(), m_loadingAssets.end(), assetId);
        if (it != m_loadingAssets.end())
        {
            LOG_ERROR("[AssetHandler] Model asset '{}' failed to load.", assetId.GetPath().c_str());
            m_loadingAssets.erase(it);
        }
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
