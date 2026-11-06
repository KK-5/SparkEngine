#pragma once

#include <EASTL/array.h>
#include <Base.h>
#include <ECS/Entity.h>
#include <Tick/TickBus.h>
#include <Tick/TickOrder.h>
#include <RHI/Context/UniqueRHIHandle.h>

namespace Spark::Resource
{
    class ModelAsset;
    class ShaderAsset;
}

namespace Spark::SandBox
{
    class RayTracingSceneFeature : public TickBus::Handler
    {
    public:
        RayTracingSceneFeature();
        ~RayTracingSceneFeature();

        bool Init();
        void Shutdown();
        void OnTick(const FrameTime& time) override;
        unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(RenderSystemTickOrder) - 1;
        }

    private:
        void CreatePass();
        void Update();

        Ptr<Resource::ModelAsset> m_model;
        Ptr<Resource::ShaderAsset> m_shader;
        eastl::array<Entity, 2> m_cubes{ NullEntity, NullEntity };
        RHI::UniqueRHIHandle m_view;
        float m_rotationAngle = 0.0f;
    };
}
