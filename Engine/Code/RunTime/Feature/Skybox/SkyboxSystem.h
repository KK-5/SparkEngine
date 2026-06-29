#pragma once

#include <ECS/ISystem.h>
#include <ECS/SystemTraits.h>
#include <ECS/Common.h>
#include <ECS/Bus/ComponentEventBus.h>

#include "Components.h"

namespace Spark::Skybox
{
    //! Data-driven skybox management system (mirrors MeshSystem). Watches
    //! SkyboxComponent on world entities; when an asset is assigned, it triggers
    //! the image-resource load flow and uploads the (cubemap-shaped) texture to
    //! the GPU, writing SkyboxGPUComponent for the render side to sample.
    //!
    //! Bake-agnostic by design: this system consumes a cubemap, it never converts
    //! an equirect. The equirect→cubemap bake is an asset / Image-Compile concern.
    class SkyboxSystem final : public ISystem,
                               public ComponentEventBus::Handler
    {
    public:
        SPARK_COMPONENT_ACCESS(
            ReadWriteComponent<SkyboxComponent>,
            WriteComponent<SkyboxGPUComponent>
        );

        SPARK_SYSTEM_TRAITS(SkyboxSystem);

        void InitInternal() override;
        void ShutdownInternal() override;

        eastl::vector<HashString> Request() const override { return {}; }
        HashString GetName() const override { return "SkyboxSystem"; }

        // ComponentEventBus
        void OnComponentConstruct(Entity entity) override;
        void OnComponentUpdated(Entity entity) override;
        void OnComponentDestory(Entity entity) override;

    private:
        //! Load-if-needed + GPU build for the entity's SkyboxComponent.
        void Resolve(Entity entity);
        void BuildGPUResources(Entity entity, const Ptr<Resource::ImageAsset>& asset);
        void CleanupGPUResources(Entity entity);
    };
}
