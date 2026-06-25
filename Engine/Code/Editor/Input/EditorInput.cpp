#include "EditorInput.h"

#include <Log/ILogSystem.h>
#include <ECS/ExecuteContext.h>
#include <SceneManager/SceneManager.h>

#include <Feature/Camera/Components.h>
#include <Feature/Transform/Components.h>
#include <Math/MathUtils.h>

namespace Editor
{
    using namespace Spark;
    using namespace Spark::Input;

    void EditorInputSystem::InitInternal()
    {
        InputEventBus::Handler::BusConnect(InputBusId::Editor);
        TickBus::Handler::BusConnect();
        FindOrCreateEditorCamera();
    }

    void EditorInputSystem::ShutdownInternal()
    {
        TickBus::Handler::BusDisconnect();
        if (InputEventBus::Handler::BusIsConnectedId(InputBusId::Editor))
        {
            InputEventBus::Handler::BusDisconnect(InputBusId::Editor);
        }
    }

    void EditorInputSystem::FindOrCreateEditorCamera()
    {
        auto& world = *WorldExecuteContext::Current();

        auto view = world.GetView<Camera::CameraComponent, Transform::TransformComponent>();
        view.each([&](Entity entity, const Camera::CameraComponent&, const Transform::TransformComponent&)
        {
            m_editorCamera = entity;
        });
        if (m_editorCamera != NullEntity)
        {
            LOG_INFO("[EditorInput] Found existing camera entity.");
            return;
        }

        m_editorCamera = world.CreateEntity("EditorCamera");
        world.Add<Transform::TransformComponent>(m_editorCamera, Transform::TransformComponent{});
        world.Add<Camera::CameraComponent>(m_editorCamera, Camera::CameraComponent{});
        Service<IScene>::Get()->AddEntity(m_editorCamera);
        LOG_INFO("[EditorInput] Created editor camera entity.");
    }

    void EditorInputSystem::OnMouseButtonEvent(MouseButtonEvent event)
    {
        if (event.button == MouseButton::Left)
        {
            m_mouseLeftHeld = (event.state == InputState::Press);
            if (m_mouseLeftHeld)
            {
                m_lastMouseX = event.xPos;
                m_lastMouseY = event.yPos;
            }
        }
    }

    void EditorInputSystem::OnMouseCursorPosEvent(MouseCursorPosEvent event)
    {
        if (!m_mouseLeftHeld)
        {
            return;
        }

        float dx = event.xPos - m_lastMouseX;
        float dy = event.yPos - m_lastMouseY;
        m_lastMouseX = event.xPos;
        m_lastMouseY = event.yPos;

        if (m_editorCamera == NullEntity)
        {
            return;
        }

        auto& world = *WorldExecuteContext::Current();
        auto* transform = world.TryGet<Transform::TransformComponent>(m_editorCamera);
        if (!transform)
        {
            return;
        }

        transform->m_rotation.y += dx * m_rotateSpeed;
        transform->m_rotation.x += dy * m_rotateSpeed;

        const float pitchLimit = 89.f;
        transform->m_rotation.x = Math::Clamp(transform->m_rotation.x, -pitchLimit, pitchLimit);
    }

    void EditorInputSystem::OnKeyboardEvent(KeyboardEvent event)
    {
        bool pressed = (event.state != InputState::Release);
        switch (event.button)
        {
        case Key::AlphanumericW: m_keyW = pressed; break;
        case Key::AlphanumericA: m_keyA = pressed; break;
        case Key::AlphanumericS: m_keyS = pressed; break;
        case Key::AlphanumericD: m_keyD = pressed; break;
        case Key::AlphanumericQ: m_keyQ = pressed; break;
        case Key::AlphanumericE: m_keyE = pressed; break;
        default: break;
        }
    }

    void EditorInputSystem::OnTick(float deltaTime)
    {
        if (m_editorCamera == NullEntity)
        {
            return;
        }

        if (!m_keyW && !m_keyA && !m_keyS && !m_keyD && !m_keyQ && !m_keyE)
        {
            return;
        }

        auto& world = *WorldExecuteContext::Current();
        auto* transform = world.TryGet<Transform::TransformComponent>(m_editorCamera);
        if (!transform)
        {
            return;
        }

        float yawRad = Math::Radians(transform->m_rotation.y);

        Math::Vector3 forward(std::sin(yawRad), 0.f, std::cos(yawRad));
        Math::Vector3 right(std::cos(yawRad), 0.f, -std::sin(yawRad));
        Math::Vector3 up(0.f, 1.f, 0.f);

        Math::Vector3 movement(0.f);
        if (m_keyW) { movement += forward; }
        if (m_keyS) { movement -= forward; }
        if (m_keyD) { movement += right; }
        if (m_keyA) { movement -= right; }
        if (m_keyQ) { movement += up; }
        if (m_keyE) { movement -= up; }

        if (Math::Length(movement) > 0.f)
        {
            movement = Math::Normalize(movement);
        }

        transform->m_position += movement * (m_moveSpeed * deltaTime);
    }
}
