#pragma once

#include <ECS/ISystem.h>
#include <ECS/Common.h>
#include <Tick/TickBus.h>
#include <Tick/TickOrder.h>

namespace Spark::Light
{
    //! Upper-layer light system. Each frame it resolves every LightComponent's world-space
    //! direction (Transform forward) and position (Transform translation) into a
    //! LightRenderData component. It also creates the always-present default directional
    //! light on Init.
    //!
    //! This system OWNS all of the light's spatial computation. By the time the render side
    //! (SceneBindingSystem) runs, it only marshals the finished LightRenderData into GPU
    //! memory — the "compute vs write" split. Symmetric to CameraSystem
    //! (WorldTransformMatrix -> CameraViewMatrix). Ticks at TICK_PRE_RENDER, after
    //! TransformSystem (which produces WorldTransformMatrix) and before RenderSystem.
    class LightSystem final : public ISystem,
                              public TickBus::Handler
    {
    public:
        eastl::vector<HashString> Request() const override { return {}; }
        HashString GetName() const override { return "LightSystem"; }

        void OnTick(float deltaTime) override;
        unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(TickOrder::TICK_PRE_RENDER);
        }

    private:
        void InitInternal() override;
        void ShutdownInternal() override;
    };
}
