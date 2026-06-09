#pragma once

#include "UI/Bus/AssetEditBus.h"

namespace Spark::Resource
{
    class ModelAsset;
}

namespace Editor
{
    class AssetHandler final : public AssetEditBus::Handler
    {
    public:
        AssetHandler();
        ~AssetHandler() override;

        void OnModelAssetDragToScene(const Spark::Resource::ModelAsset& asset) override;
    };
}
