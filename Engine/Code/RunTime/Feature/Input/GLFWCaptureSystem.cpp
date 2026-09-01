#include "GLFWCaptureSystem.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include "../Window/IWindowSystem.h"
#include "../UI/UIBaseSystem.h"

#include "Bus/InputEventBus.h"

namespace Spark::Input
{
    void GLFWCaptureSystem::InitInternal()
    {
        using namespace Spark::Window;
        InputCaptureSystem::InitInternal();

        if (Service<IWindowSystem>::Get()->GetWindowBackend() != WindowBackend::GLFW)
        {
            ASSERT(false, "[GLFWCaptureSystem] The window backend does not match");
        }

        if (void* window = Service<IWindowSystem>::Get()->GetWindowHandle())
        {
            m_windowCache = static_cast<GLFWwindow*>(window);
        }
        ASSERT(m_windowCache, "[GLFWCaptureSystem] Get glfw window failed!");
        glfwSetWindowUserPointer(m_windowCache, this);

        CaptureMouseButtonEvent();
        CaptureMouseCursorPosEvent();
        CaptureMouseScrollEvent();
        CaptureKeyboardEvent();
        CaptureWindowCloseEvent();
        CaptureWindowResizeEvent();
    }

    void GLFWCaptureSystem::CaptureMouseButtonEvent()
    {
        glfwSetMouseButtonCallback(
            m_windowCache,
            [](GLFWwindow* window, int button, int action, int mods)
        {
            GLFWCaptureSystem* thisPointer = static_cast<GLFWCaptureSystem*>(glfwGetWindowUserPointer(window));

            MouseButtonEvent event;
            event.button = s_mouseButtonMap[button];
            event.state  = s_inputStateMap[action];
            event.mode   = s_inputModMap.find(mods) != s_inputModMap.end() ? s_inputModMap[mods] : InputMode::Invalid;

            {
                double x, y;
                glfwGetCursorPos(window, &x, &y);
                event.xPos = static_cast<float>(x);
                event.yPos = static_cast<float>(y);
            }

            // A press is routed to one side or the other. A release goes to BOTH, always:
            // losing a press only means an interaction never starts, while losing a release
            // leaves whoever saw the press believing the button is still down forever. The
            // two costs are not symmetric, and the answer can differ between the press and
            // the release of the same click -- the cursor moves in between.
            const bool released = (event.state == InputState::Release);

            if (!released)
            {
                if (auto ui = Service<UI::UIBaseSystem>::Get())
                {
                    if (ui->WantCaptureMouse())
                    {
                        InputEventBus::Event(InputBusId::EditorUI, &InputEventBus::Events::OnMouseButtonEvent, event);
                        return;
                    }
                }
            }
            else
            {
                InputEventBus::Event(InputBusId::EditorUI, &InputEventBus::Events::OnMouseButtonEvent, event);
            }

            InputEventBus::Event(InputBusId::Editor, &InputEventBus::Events::OnMouseButtonEvent, event);
        });
    }

    void GLFWCaptureSystem::CaptureMouseCursorPosEvent()
    {
        glfwSetCursorPosCallback(
            m_windowCache, 
            [](GLFWwindow* window, double x, double y) 
        {
            GLFWCaptureSystem* thisPointer = static_cast<GLFWCaptureSystem*>(glfwGetWindowUserPointer(window));

            MouseCursorPosEvent event;
            event.xPos = (float)x;
            event.yPos = (float)y;

            InputEventBus::Event(InputBusId::EditorUI, &InputEventBus::Events::OnMouseCursorPosEvent, event);

            if (auto ui = Service<UI::UIBaseSystem>::Get())
            {
                if (ui->WantCaptureMouse())
                {
                    return;
                }
            }

            InputEventBus::Event(InputBusId::Editor, &InputEventBus::Events::OnMouseCursorPosEvent, event);
        });
    }

    void GLFWCaptureSystem::CaptureMouseScrollEvent()
    {
        glfwSetScrollCallback(
            m_windowCache,
            [](GLFWwindow* window, double xOffset, double yOffset)
        {
            GLFWCaptureSystem* thisPointer = static_cast<GLFWCaptureSystem*>(glfwGetWindowUserPointer(window));

            MouseScrollEvent event;
            event.xOffset = (float)xOffset;
            event.yOffset = (float)yOffset;

            if (auto ui = Service<UI::UIBaseSystem>::Get())
            {
                if (ui->WantCaptureMouse())
                {
                    InputEventBus::Event(InputBusId::EditorUI, &InputEventBus::Events::OnMouseScrollEvent, event);
                    return;
                }
            }

            InputEventBus::Event(InputBusId::Editor, &InputEventBus::Events::OnMouseScrollEvent, event);
        });
    }

    void GLFWCaptureSystem::CaptureKeyboardEvent()   
    {
        glfwSetKeyCallback(
            m_windowCache,
            [](GLFWwindow* window, int key, int scancode, int action, int mods)
        {
            GLFWCaptureSystem* thisPointer = static_cast<GLFWCaptureSystem*>(glfwGetWindowUserPointer(window));

            KeyboardEvent event;
            event.button = s_inputKeyMap.find(key) != s_inputKeyMap.end() ? s_inputKeyMap[key] : Key::Invalid;
            event.state  = s_inputStateMap[action];
            event.mode   = s_inputModMap.find(mods) != s_inputModMap.end() ? s_inputModMap[mods] : InputMode::Invalid;

            // Keyboard routing asks about the keyboard. Asking WantCaptureMouse instead made
            // it depend on where the cursor happened to be, so typing into a field while the
            // cursor sat over the scene drove the camera at the same time.
            if (auto ui = Service<UI::UIBaseSystem>::Get())
            {
                if (ui->WantCaptureKeyboard())
                {
                    InputEventBus::Event(InputBusId::EditorUI, &InputEventBus::Events::OnKeyboardEvent, event);
                    return;
                }
            }

            InputEventBus::Event(InputBusId::Editor, &InputEventBus::Events::OnKeyboardEvent, event);
        });
    }

    void GLFWCaptureSystem::CaptureWindowCloseEvent()
    {
        glfwSetWindowCloseCallback(
            m_windowCache,
            [](GLFWwindow* window)
        {
            GLFWCaptureSystem* thisPointer = static_cast<GLFWCaptureSystem*>(glfwGetWindowUserPointer(window));

            InputEventBus::Broadcast(&InputEventBus::Events::OnWindowCloseEvnet);
        });
    }

    void GLFWCaptureSystem::CaptureWindowResizeEvent()
    {
        glfwSetWindowSizeCallback(
            m_windowCache, 
        [](GLFWwindow* window, int width, int height) 
        {
            GLFWCaptureSystem* thisPointer = static_cast<GLFWCaptureSystem*>(glfwGetWindowUserPointer(window));

            WindowResizeEvent event;
            event.width = width;
            event.height = height;

            InputEventBus::Broadcast(&InputEventBus::Events::OnWindowResizeEvent, event);
        });
    }
}