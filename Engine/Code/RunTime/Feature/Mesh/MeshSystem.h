#pragma once

#include <ECS/ISystem.h>
#include <ECS/SystemTraits.h>
#include <ECS/Common.h>
#include <ECS/Bus/ComponentEventBus.h>

#include "Components.h"

namespace Spark::Mesh
{
    //! Checks that a MeshComponent names a mesh that exists and fills its statistics. Putting
    //! the geometry on the GPU is the render side's (Render::MeshGeometrySystem).
    class MeshSystem final : public ISystem,
                             public ComponentEventBus::Handler
    {
    public:
        SPARK_COMPONENT_ACCESS(
            ReadWriteComponent<MeshComponent>
        );

        SPARK_SYSTEM_TRAITS(MeshSystem);

        void InitInternal() override;
        void ShutdownInternal() override;

        eastl::vector<HashString> Request() const override { return {}; }
        HashString GetName() const override { return "MeshSystem"; }

        // ComponentEventBus
        void OnComponentConstruct(Entity entity) override;
        void OnComponentUpdated(Entity entity) override;

    private:
        void UpdateStatistics(MeshComponent& meshComp, Resource::ModelAsset& modelAsset);
    };
}
