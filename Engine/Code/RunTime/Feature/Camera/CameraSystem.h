#pragma once

#include <ECS/ISystem.h>
#include <ECS/SystemTraits.h>
#include <ECS/Common.h>
#include <Tick/TickBus.h>

#include <Transform/Components.h>

#include "Components.h"

namespace Spark::Camera
{
    class CameraSystem final : public ISystem,
                               public TickBus::Handler
    {
    public:
        SPARK_COMPONENT_ACCESS(
            ReadComponent<CameraComponent>,
            ReadComponent<Transform::WorldTransformMatrix>,
            WriteComponent<CameraViewMatrix>
        );

        SPARK_SYSTEM_TRAITS(CameraSystem);

        // ISystem
        void InitInternal() override;
        void ShutdownInternal() override;

        eastl::vector<HashString> Request() const override
        {
            return {};
        }

        HashString GetName() const override
        {
            return "";
        }

        // TickBus
        void OnTick(float deltaTime) override;

        inline unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(TickOrder::TICK_PRE_RENDER);
        }
    };
}
