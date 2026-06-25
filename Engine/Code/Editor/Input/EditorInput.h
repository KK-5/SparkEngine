#pragma once

#include <ECS/ISystem.h>
#include <ECS/WorldContext.h>
#include <Feature/Input/Bus/InputEventBus.h>
#include <Tick/TickBus.h>

namespace Editor
{
    class EditorInputSystem : public Spark::ISystem,
                              public Spark::TickBus::Handler,
                              public Spark::Input::InputEventBus::Handler
    {
    public:
        // ISystem
        void InitInternal() override;
        void ShutdownInternal() override;

        eastl::vector<Spark::HashString> Request() const override
        {
            return {"InputSystem"_hs};
        }

        Spark::HashString GetName() const override
        {
            return "EditorInputSystem"_hs;
        }

        // TickBus
        void OnTick(float deltaTime) override;

        inline unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(Spark::TickOrder::TICK_INPUT) + 1;
        }

        // InputEventBus
        void OnMouseButtonEvent(Spark::Input::MouseButtonEvent event) override;
        void OnMouseCursorPosEvent(Spark::Input::MouseCursorPosEvent event) override;
        void OnKeyboardEvent(Spark::Input::KeyboardEvent event) override;

    private:
        void FindOrCreateEditorCamera();

        float m_moveSpeed    = 10.f;
        float m_rotateSpeed  = 0.1f;

        bool m_keyW = false;
        bool m_keyA = false;
        bool m_keyS = false;
        bool m_keyD = false;
        bool m_keyQ = false;
        bool m_keyE = false;

        bool m_mouseLeftHeld  = false;
        float m_lastMouseX = 0.f;
        float m_lastMouseY = 0.f;

        Spark::Entity m_editorCamera = Spark::NullEntity;
    };
}
