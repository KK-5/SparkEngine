#pragma once

#include <ECS/Common.h>
#include <Resource/Bus/AssetResolveBus.h>

namespace Spark::Mesh
{
    class MeshResolver final : public Resource::AssetResolveBus::Handler
    {
    public:
        MeshResolver() = default;
        ~MeshResolver();

        void Init();

        void ResolveModelAssetToScene(Ptr<Resource::ModelAsset> asset) override;
    };
}
