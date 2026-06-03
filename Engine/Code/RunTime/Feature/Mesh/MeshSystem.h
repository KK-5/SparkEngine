#pragma once

#include <ECS/ISystem.h>
#include <ECS/SystemTraits.h>
#include <ECS/Common.h>
#include <ECS/Bus/ComponentEventBus.h>

#include <Resource/AssetTypes.h>
#include <Resource/Bus/AssetBus.h>

#include "Components.h"

namespace Spark::Mesh
{
    class MeshSystem final : public ISystem,
                             public ComponentEventBus::Handler,
                             public Resource::AssetBus::Handler
    {
    public:
        SPARK_COMPONENT_ACCESS(
            ReadWriteComponent<MeshComponent>,
            ReadWriteComponent<MeshAssetLoadingTag>,
            WriteComponent<MeshGPUComponent>
        );

        SPARK_SYSTEM_TRAITS(MeshSystem);

        void InitInternal() override;
        void ShutdownInternal() override;

        eastl::vector<HashString> Request() const override { return {}; }
        HashString GetName() const override { return "MeshSystem"; }

        // ComponentEventBus
        void OnComponentConstruct(Entity entity) override;
        void OnComponentUpdated(Entity entity) override;
        void OnComponentDestory(Entity entity) override;

        // AssetBus
        void OnAssetReady(Resource::Asset& asset) override;
        void OnAssetError(Resource::Asset& asset) override;

    private:
        void ProcessMeshEntity(Entity entity);
        void BuildGPUResources(Entity entity, Resource::ModelAsset& modelAsset);
        void CleanupGPUResources(Entity entity);
    };
}
