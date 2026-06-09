#include "AssetHandler.h"

#include <ECS/ExecuteContext.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Model/ModelAsset.h>
#include <Mesh/MeshUtils.h>

#include "UI/Bus/AssetEditBus.h"

namespace Editor
{
    AssetHandler::AssetHandler()
    {
        AssetEditBus::Handler::BusConnect();
    }

    AssetHandler::~AssetHandler()
    {
        AssetEditBus::Handler::BusDisconnect();
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
            LOG_INFO("[AssetHandler] Extracting model asset '{}' to world.", asset.GetName().GetCStr());
            Ptr<Resource::ModelAsset> model(const_cast<Resource::ModelAsset*>(&asset));
            Mesh::ExtractMeshToWorld(model, *worldCtx);
            return;
        }

        if (asset.IsLoading())
        {
            LOG_WARN("[AssetHandler] Model asset '{}' is still loading.", asset.GetName().GetCStr());
            return;
        }

        auto* am = Service<Resource::AssetManager>::Get();
        if (!am)
        {
            LOG_ERROR("[AssetHandler] AssetManager service is unavailable.");
            return;
        }

        LOG_INFO("[AssetHandler] Loading model asset '{}'...", asset.GetName().GetCStr());
        Ptr<Resource::Asset> loaded = am->LoadAsset(asset.GetAssetId(), asset.GetAssetType());
        if (loaded && loaded->IsReady())
        {
            Ptr<Resource::ModelAsset> model(static_cast<Resource::ModelAsset*>(loaded.get()));
            Mesh::ExtractMeshToWorld(model, *worldCtx);
        }
        else
        {
            LOG_ERROR("[AssetHandler] Failed to load model asset '{}'.", asset.GetName().GetCStr());
        }
    }
}
