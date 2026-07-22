#include "CameraSystem.h"

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <Math/MathUtils.h>

namespace Spark::Camera
{
    void CameraSystem::InitInternal()
    {
        TickBus::Handler::BusConnect();
    }

    void CameraSystem::ShutdownInternal()
    {
        TickBus::Handler::BusDisconnect();
    }

    void CameraSystem::OnTick(float deltaTime)
    {
        auto& world = *WorldExecuteContext::Current();

        // Resolve only the world->view transform here. Projection is built downstream in
        // ViewBindingSystem, where the render-target aspect is known — this layer never
        // touches the render surface (or UI).
        world.GetView<CameraComponent, Transform::WorldTransformMatrix>().each(
            [&](Entity entity, const CameraComponent&, const Transform::WorldTransformMatrix& worldMatrix)
        {
            CameraViewMatrix matrices;
            matrices.m_viewMatrix = Math::Inverse(worldMatrix.m_worldMatrix);
            world.AddOrReplace<CameraViewMatrix>(entity, matrices);
        });
    }
}
