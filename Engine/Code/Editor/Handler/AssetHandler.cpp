#include "AssetHandler.h"

#include <EASTL/algorithm.h>

#include <ECS/ExecuteContext.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Model/ModelAsset.h>
#include <Resource/Asset.h>
#include <Mesh/MeshUtils.h>
#include <Resource/Bus/AssetResolveBus.h>

namespace Editor
{
    AssetHandler::AssetHandler()
    {
        AssetEditBus::Handler::BusConnect();
        Spark::Resource::AssetBus::Handler::BusConnect(Spark::Resource::AssetType::Model);
    }

    AssetHandler::~AssetHandler()
    {
        AssetEditBus::Handler::BusDisconnect();
        Spark::Resource::AssetBus::Handler::BusDisconnect();
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
            Mesh::ExtractMeshToWorld(model, *worldCtx);
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
        Ptr<Resource::Asset> requested = am->RequestAsset(assetId, asset.GetAssetType());
        if (requested)
        {
            m_loadingAssets.push_back(assetId);
        }
        else
        {
            LOG_ERROR("[AssetHandler] Failed to request model asset '{}'.", asset.GetName().GetCStr());
        }
    }

    void AssetHandler::OnAssetReady(Spark::Resource::Asset& asset)
    {
        using namespace Spark;

        const Resource::AssetId& assetId = asset.GetAssetId();
        auto it = eastl::find(m_loadingAssets.begin(), m_loadingAssets.end(), assetId);
        if (it == m_loadingAssets.end())
        {
            return;
        }

        LOG_INFO("[AssetHandler] Model asset '{}' ready, queuing scene resolve.", asset.GetName().GetCStr());
        auto* modelAsset = static_cast<Resource::ModelAsset*>(&asset);
        Ptr<Resource::ModelAsset> model(modelAsset);
        Resource::AssetResolveBus::QueueBroadcast(
            &Resource::AssetResolveBusTraits::ResolveModelAssetToScene, eastl::move(model));
        m_loadingAssets.erase(it);
    }

    void AssetHandler::OnAssetError(Spark::Resource::Asset& asset)
    {
        using namespace Spark;

        const Resource::AssetId& assetId = asset.GetAssetId();
        auto it = eastl::find(m_loadingAssets.begin(), m_loadingAssets.end(), assetId);
        if (it != m_loadingAssets.end())
        {
            LOG_ERROR("[AssetHandler] Model asset '{}' failed to load.", asset.GetName().GetCStr());
            m_loadingAssets.erase(it);
        }
    }
}
