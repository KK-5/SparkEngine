#pragma once

#include <GLFW/glfw3.h>

#include <EASTL/fixed_hash_map.h>

#include "InputEvent.h"
#include "InputCaptureSystem.h"

namespace Spark::Input
{

    static eastl::fixed_hash_map<int, MouseButton, 3> s_mouseButtonMap = 
    {
        {GLFW_MOUSE_BUTTON_LEFT,      MouseButton::Left},
        {GLFW_MOUSE_BUTTON_RIGHT,     MouseButton::Right},
        {GLFW_MOUSE_BUTTON_MIDDLE,    MouseButton::Middle}
    };

    static eastl::fixed_hash_map<int, InputState,  3>   s_inputStateMap =
    {
        {GLFW_PRESS,                  InputState::Press},
        {GLFW_RELEASE,                InputState::Repeat},
        {GLFW_REPEAT,                 InputState::Release},
    };

    static eastl::fixed_hash_map<int, InputMode,  3>      s_inputModMap =
    {
        {GLFW_MOD_SHIFT,              InputMode::Shift},
        {GLFW_MOD_CONTROL,            InputMode::Control},
        {GLFW_MOD_ALT,                InputMode::Alt}
    };

    static eastl::fixed_hash_map<int, Key,        8>      s_inputKeyMap =
    {
        {GLFW_KEY_A,                  Key::AlphanumericA},
        {GLFW_KEY_D,                  Key::AlphanumericD},
        {GLFW_KEY_E,                  Key::AlphanumericE},
        {GLFW_KEY_Q,                  Key::AlphanumericQ},
        {GLFW_KEY_R,                  Key::AlphanumericR},
        {GLFW_KEY_S,                  Key::AlphanumericS},
        {GLFW_KEY_W,                  Key::AlphanumericW},
        {GLFW_KEY_SPACE,              Key::Space}
    };

    class GLFWCaptureSystem final: public InputCaptureSystem
    {
    public:
        // ISystem
        void Initialize() override;

    protected:
        // InputCaptureSystem
        void CaptureMouseButtonEvent()     override;
        void CaptureMouseCursorPosEvent()  override;
        void CaptureMouseScrollEvent()     override;
        void CaptureKeyboardEvent()        override;
        void CaptureWindowCloseEvent()     override;
        void CaptureWindowResizeEvent()    override;
    
    private:
        GLFWwindow*     m_windowCache;
    };
    
} // namespace name
