#include "MeshResolver.h"

#include <ECS/ExecuteContext.h>
#include <Resource/Model/ModelAsset.h>

#include "MeshUtils.h"

namespace Spark::Mesh
{
    MeshResolver::~MeshResolver()
    {
        if (BusIsConnected())
        {
            BusDisconnect();
        }
    }

    void MeshResolver::Init()
    {
        Resource::AssetResolveBus::Handler::BusConnect();
    }

    void MeshResolver::ResolveModelAssetToScene(Ptr<Resource::ModelAsset> asset)
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

        ExtractMeshToWorld(asset, *world);
    }
}
