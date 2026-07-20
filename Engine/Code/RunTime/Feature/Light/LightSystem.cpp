#include "LightSystem.h"

#include <cmath>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <Service/Service.h>
#include <Math/MathUtils.h>
#include <SceneManager/IScene.h>

#include <Transform/Components.h>

#include "Components.h"

namespace Spark::Light
{
    namespace
    {
        // The light shines along its Transform's forward axis. In the world matrix the
        // forward axis is the Z basis column (column 2), matching the camera's LH
        // +Z-forward convention. Extracting it here is the ONLY spatial computation for a
        // light — the render side never sees a transform.
        Math::Vector3 ExtractForward(const Math::Matrix4X4& world)
        {
            return Math::Normalize(Math::Vector3(world[2]));
        }
        Math::Vector3 ExtractPosition(const Math::Matrix4X4& world)
        {
            return Math::Vector3(world[3]);
        }
    }

    void LightSystem::InitInternal()
    {
        auto* world = WorldExecuteContext::Current();
        if (world)
        {
            // Default directional light reproducing the previously hardcoded one in
            // Lighting.hlsl: shines along normalize(-0.5,-1,-0.3), warm white, intensity 3.
            // Orient the transform so its forward (Z column) equals that direction: a
            // look-rotation, stored back as the Euler angles TransformComponent expects
            // (degrees; TransformSystem re-applies radians()).
            const Math::Vector3 dir = Math::Normalize(Math::Vector3(-0.5f, -1.0f, -0.3f));
            const Math::Quaternion rot = Math::QuaternionLookAt(dir, Math::Vector3(0.0f, 1.0f, 0.0f));
            const Math::Vector3 eulerRad = Math::QuaternionToEuler(rot);
            const Math::Vector3 eulerDeg{
                Math::Degrees(eulerRad.x), Math::Degrees(eulerRad.y), Math::Degrees(eulerRad.z) };

            Entity light = world->CreateEntity("DirectionalLight");

            Transform::TransformComponent xform;
            xform.m_rotation = eulerDeg;
            world->Add<Transform::TransformComponent>(light, xform);

            LightComponent lc;
            lc.m_type      = LightType::Directional;
            lc.m_color     = Math::Vector4(1.0f, 0.98f, 0.95f, 1.0f);
            lc.m_intensity = 3.0f;
            world->Add<LightComponent>(light, lc);

            if (auto* scene = Service<IScene>::Get())
            {
                scene->AddEntity(light);
            }
        }

        TickBus::Handler::BusConnect();
    }

    void LightSystem::ShutdownInternal()
    {
        TickBus::Handler::BusDisconnect();
    }

    void LightSystem::OnTick(float /*deltaTime*/)
    {
        auto* world = WorldExecuteContext::Current();
        if (!world)
        {
            return;
        }

        world->GetView<LightComponent, Transform::WorldTransformMatrix>().each(
            [&](Entity entity, const LightComponent& lc, const Transform::WorldTransformMatrix& xform)
        {
            LightRenderData rd;
            rd.m_type           = lc.m_type;
            rd.m_color          = Math::Vector3(lc.m_color); // authoring color is rgba; render side is rgb
            rd.m_intensity      = lc.m_intensity;
            rd.m_range          = lc.m_range;
            rd.m_worldDirection = ExtractForward(xform.m_worldMatrix);
            rd.m_worldPosition  = ExtractPosition(xform.m_worldMatrix);
            rd.m_cosInner       = std::cos(Math::Radians(lc.m_innerConeDeg));
            rd.m_cosOuter       = std::cos(Math::Radians(lc.m_outerConeDeg));

            world->AddOrReplace<LightRenderData>(entity, rd);
        });
    }
}
