#pragma once

#include <ECS/Common.h>
#include <Resource/Bus/AssetResolveBus.h>

namespace Spark::Spawn
{
    //! Handles AssetResolveBus::ResolveModelAssetToScene: once a dragged-in model asset
    //! finishes loading, instantiate it into the world via SpawnModel. Model-specific by
    //! design — a future prefab path gets its own PrefabSpawnResolver in this module.
    class ModelSpawnResolver final : public Resource::AssetResolveBus::Handler
    {
    public:
        ModelSpawnResolver() = default;
        ~ModelSpawnResolver();

        void Init();

        void ResolveModelAssetToScene(Ptr<Resource::ModelAsset> asset) override;
    };
}
