#include "GLFWCaptureSystem.h"

#include <Log/SpdLogSystem.h>
#include <Service/Service.h>

#include "../Window/IWindowSystem.h"
#include "../UI/UIBaseSystem.h"

#include "Bus/InputEventBus.h"

namespace Spark::Input
{
    void GLFWCaptureSystem::Initialize()
    {
        using namespace Spark::Window;
        InputCaptureSystem::Initialize();

        if (Service<IWindowSystem>::Get()->GetWindowBackend() != WindowBackend::GLFW)
        {
            LOG_ERROR("[GLFWCaptureSystem] The window backend does not match");
            assert(false);
        }

        if (void* window = Service<IWindowSystem>::Get()->GetWindowHandle())
        {
            m_windowCache = static_cast<GLFWwindow*>(window);
        }
        assert(m_windowCache && "[GLFWCaptureSystem] Get glfw window failed!");
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

            if (auto ui = Service<UI::UIBaseSystem>::Get())
            {
                if (ui->WantCaptureMouse())
                {
                    InputEventBus::Event(InputBusId::EditorUI, &InputEventBus::Events::OnMouseButtonEvent, thisPointer->m_contextRef.value(), event);
                    return;
                }
            }

            InputEventBus::Event(InputBusId::Editor, &InputEventBus::Events::OnMouseButtonEvent, thisPointer->m_contextRef.value(), event);
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

            InputEventBus::Event(InputBusId::EditorUI, &InputEventBus::Events::OnMouseCursorPosEvent, thisPointer->m_contextRef.value(), event);

            if (auto ui = Service<UI::UIBaseSystem>::Get())
            {
                if (ui->WantCaptureMouse())
                {
                    return;
                }
            }

            InputEventBus::Event(InputBusId::Editor, &InputEventBus::Events::OnMouseCursorPosEvent, thisPointer->m_contextRef.value(), event);
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
                    InputEventBus::Event(InputBusId::EditorUI, &InputEventBus::Events::OnMouseScrollEvent, thisPointer->m_contextRef.value(), event);
                    return;
                }
            }

            InputEventBus::Event(InputBusId::Editor, &InputEventBus::Events::OnMouseScrollEvent, thisPointer->m_contextRef.value(), event);
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

            if (auto ui = Service<UI::UIBaseSystem>::Get())
            {
                if (ui->WantCaptureMouse())
                {
                    InputEventBus::Event(InputBusId::EditorUI, &InputEventBus::Events::OnKeyboardEvent, thisPointer->m_contextRef.value(), event);
                    return;
                }
            }
           
            InputEventBus::Event(InputBusId::Editor, &InputEventBus::Events::OnKeyboardEvent, thisPointer->m_contextRef.value(), event);
        });
    }

    void GLFWCaptureSystem::CaptureWindowCloseEvent()
    {
        glfwSetWindowCloseCallback(
            m_windowCache,
            [](GLFWwindow* window)
        {
            GLFWCaptureSystem* thisPointer = static_cast<GLFWCaptureSystem*>(glfwGetWindowUserPointer(window));

            InputEventBus::Broadcast(&InputEventBus::Events::OnWindowCloseEvnet, thisPointer->m_contextRef.value());
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

            InputEventBus::Broadcast(&InputEventBus::Events::OnWindowResizeEvent, thisPointer->m_contextRef.value(), event);
        });
    }
}