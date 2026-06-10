#pragma once

#include "UI/Bus/AssetEditBus.h"
#include <Resource/Bus/AssetBus.h>
#include <EASTL/vector.h>

namespace Spark::Resource
{
    class ModelAsset;
}

namespace Editor
{
    class AssetHandler final : public AssetEditBus::Handler,
                               public Spark::Resource::AssetBus::Handler
    {
    public:
        AssetHandler();
        ~AssetHandler() override;

        void OnModelAssetDragToScene(const Spark::Resource::ModelAsset& asset) override;
        void OnAssetReady(Spark::Resource::Asset& asset) override;
        void OnAssetError(Spark::Resource::Asset& asset) override;

    private:
        eastl::vector<Spark::Resource::AssetId> m_loadingAssets;
    };
}
