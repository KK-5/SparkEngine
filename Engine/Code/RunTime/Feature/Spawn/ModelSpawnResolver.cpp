#include "ModelSpawnResolver.h"

#include <ECS/ExecuteContext.h>
#include <Resource/Model/ModelAsset.h>

#include "SpawnModel.h"

namespace Spark::Spawn
{
    ModelSpawnResolver::~ModelSpawnResolver()
    {
        if (BusIsConnected())
        {
            BusDisconnect();
        }
    }

    void ModelSpawnResolver::Init()
    {
        Resource::AssetResolveBus::Handler::BusConnect();
    }

    void ModelSpawnResolver::ResolveModelAssetToScene(Ptr<Resource::ModelAsset> asset)
    {
        if (!asset)
        {
            return;
        }

        auto* world = WorldExecuteContext::Current();
        if (!world)
        {
            return;
        }

        SpawnModel(asset, *world);
    }
}
