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

        world.GetView<CameraComponent, Transform::WorldTransformMatrix>().each(
            [&](Entity entity, const CameraComponent& camera, const Transform::WorldTransformMatrix& worldMatrix)
        {
            CameraViewMatrix matrices;
            matrices.m_viewMatrix = Math::Inverse(worldMatrix.m_worldMatrix);
            matrices.m_projectionMatrix = Math::PerspectiveFov(
                Math::Radians(camera.m_fov),
                camera.m_aspectRatio,
                camera.m_clipStart,
                camera.m_clipEnd);
            matrices.m_viewProjectionMatrix = matrices.m_projectionMatrix * matrices.m_viewMatrix;

            world.AddOrReplace<CameraViewMatrix>(entity, matrices);
        });
    }
}
