#pragma once

#include <ECS/ISystem.h>
#include <ECS/Entity.h>
#include <ECS/Bus/ComponentEventBus.h>

#include "Components.h"

namespace Spark::Skybox
{
    //! The first created skybox owns scene selection until it is removed.
    //! Later skyboxes stay inactive; only a newly created one can claim a vacant selection.
    class SkyboxSystem final : public ISystem, public ComponentEventBus::Handler
    {
    public:
        eastl::vector<HashString> Request() const override { return {}; }
        HashString GetName() const override { return "SkyboxSystem"_hs; }

        void OnComponentConstruct(Entity entity) override;
        void OnComponentDestory(Entity entity) override;

    protected:
        void InitInternal() override;
        void ShutdownInternal() override;
    };
}
