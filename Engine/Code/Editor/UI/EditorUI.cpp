#include "EditorUI.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glfw/glfw3.h>

#include <Log/SpdLogSystem.h>

namespace Editor
{
    using namespace Spark;

    void EditorUI::Initialize()
    {
        Spark::UI::UIBaseSystem::Initialize();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.FontGlobalScale = 1.2f;

        SetUpStyle();

        if (!Service<Window::IWindowSystem>::Get() ||
            Service<Window::IWindowSystem>::Get()->GetWindowBackend() != Window::WindowBackend::GLFW)
        {
            LOG_ERROR("[EditorUI] Get window bankend failed!");
            assert(false);
        }

        GLFWwindow* window = static_cast<GLFWwindow*>(Service<Window::IWindowSystem>::Get()->GetWindowHandle());
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 330");

        m_dockLayoutInit = false;
        m_menuBar = eastl::make_unique<MenuBar>();
        m_bottomPanel = eastl::make_unique<BottomPanel>();
        m_sceneView = eastl::make_unique<SceneView>();
        m_inspector = eastl::make_unique<Inspector>();
        m_componentView = eastl::make_unique<ComponentView>();

        Spark::Input::InputEventBus::Handler::BusConnect(Spark::Input::InputBusId::EditorUI);
    }

    void EditorUI::ShutDown()
    {
        if (Spark::Input::InputEventBus::Handler::BusIsConnectedId(Spark::Input::InputBusId::EditorUI))
        {
            Spark::Input::InputEventBus::Handler::BusDisconnect(Spark::Input::InputBusId::EditorUI);
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void EditorUI::SetUpStyle()
    {
        ImGui::StyleColorsDark();
    
        ImGuiStyle& style = ImGui::GetStyle();
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.WindowPadding = ImVec2(0.0f, 0.0f);
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
    }

    void EditorUI::SetupDefaultLayout(ImGuiID dockspaceId)
    {
        if (!m_dockLayoutInit) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);
            
            // 分割 DockSpace
            ImGuiID dockMain = dockspaceId;
            ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.25f, nullptr, &dockMain);
            ImGuiID dockRightTop = ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Up, 0.45f, nullptr, &dockRight);
            ImGuiID dockRightDown = dockRight;
            ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.25f, nullptr, &dockMain);
            
            ImGui::DockBuilderDockWindow("Scene View", dockMain);
            ImGui::DockBuilderDockWindow("Inspector", dockRightTop);
            ImGui::DockBuilderDockWindow("Component View", dockRightDown);
            ImGui::DockBuilderDockWindow("Browser", dockBottom);        
            
            ImGui::DockBuilderFinish(dockspaceId);

            m_dockLayoutInit = true;
        }
    }

    void EditorUI::NewFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }
    void EditorUI::DrawUI(Spark::WorldContext& context)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        
        ImGui::Begin("Main Window", nullptr, window_flags);
        ImGui::PopStyleVar(3);

        m_menuBar->Draw(context);
        
        // DockSpace
        ImGuiID dockspaceId = ImGui::GetID("DockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_AutoHideTabBar);
        SetupDefaultLayout(dockspaceId);

        ImGui::End();

        m_bottomPanel->Draw();
        m_sceneView->Draw();
        m_inspector->Draw(context);
        m_componentView->Draw(context);

        //static bool showDemoWindow = false;
        //ImGui::ShowDemoWindow(&showDemoWindow);
    }

    void EditorUI::EndFrame()
    {
        Service<Window::IWindowSystem>::Get()->SwapBuffer();
        
        // glfw依赖，应该移除？
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        } 
    }

    eastl::any EditorUI::GetUIRenderData()
    {
        ImGui::Render();
        return ImGui::GetDrawData();
    }

    bool EditorUI::WantCaptureMouse() const
    {
        return ImGui::GetIO().WantCaptureMouse;
    }

    bool EditorUI::WantCaptureKeyboard() const
    {
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    void EditorUI::OnMouseButtonEvent(Spark::WorldContext& context, Spark::Input::MouseButtonEvent event)
    {
        using namespace Spark::Input;

        // Initialize 中保证platform backend是glfw，目前只支持glfw
        GLFWwindow* window = static_cast<GLFWwindow*>(Service<Window::IWindowSystem>::Get()->GetWindowHandle());

        int button;
        if (event.button == MouseButton::Left)
        {
            button = GLFW_MOUSE_BUTTON_LEFT;
        }
        else if (event.button == MouseButton::Right)
        {
            button = GLFW_MOUSE_BUTTON_RIGHT;
        }
        else if (event.button == MouseButton::Middle)
        {
            button = GLFW_MOUSE_BUTTON_MIDDLE;
        }
        else
        {
            button = GLFW_MOUSE_BUTTON_LAST;
        }

        int action {0};
        if (event.state == InputState::Press)
        {
            action = GLFW_PRESS;
        }
        else if (event.state == InputState::Release)
        {
            action = GLFW_RELEASE;
        }
        else if (event.state == InputState::Repeat)
        {
            action = GLFW_REPEAT;
        }

        int mods {0};
        if (event.mode == InputMode::Shift)
        {
            mods |= GLFW_MOD_SHIFT;
        }
        else if (event.mode == InputMode::Control)
        {
            mods |= GLFW_MOD_CONTROL;
        }
        else if (event.mode == InputMode::Alt)
        {
            mods |= GLFW_MOD_ALT;
        }

        ImGuiIO& io = ImGui::GetIO();
        if (button >= 0 && button < ImGuiMouseButton_COUNT)
        {
            io.AddMouseButtonEvent(button, action == GLFW_PRESS);
        }
    }

    void EditorUI::OnMouseCursorPosEvent(Spark::WorldContext& context, Spark::Input::MouseCursorPosEvent event)
    {
        GLFWwindow* window = static_cast<GLFWwindow*>(Service<Window::IWindowSystem>::Get()->GetWindowHandle());

        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(event.xPos, event.yPos);
    }

    void EditorUI::OnMouseScrollEvent(Spark::WorldContext& context, Spark::Input::MouseScrollEvent event)
    {
        GLFWwindow* window = static_cast<GLFWwindow*>(Service<Window::IWindowSystem>::Get()->GetWindowHandle());

        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseWheelEvent(event.xOffset, event.yOffset);
    }

    ImGuiKey ImGui_ImplGlfw_KeyToImGuiKey(int keycode, int scancode)
    {
        IM_UNUSED(scancode);
        switch (keycode)
        {
            case GLFW_KEY_TAB: return ImGuiKey_Tab;
            case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
            case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
            case GLFW_KEY_UP: return ImGuiKey_UpArrow;
            case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
            case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
            case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
            case GLFW_KEY_HOME: return ImGuiKey_Home;
            case GLFW_KEY_END: return ImGuiKey_End;
            case GLFW_KEY_INSERT: return ImGuiKey_Insert;
            case GLFW_KEY_DELETE: return ImGuiKey_Delete;
            case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
            case GLFW_KEY_SPACE: return ImGuiKey_Space;
            case GLFW_KEY_ENTER: return ImGuiKey_Enter;
            case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
            case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
            case GLFW_KEY_COMMA: return ImGuiKey_Comma;
            case GLFW_KEY_MINUS: return ImGuiKey_Minus;
            case GLFW_KEY_PERIOD: return ImGuiKey_Period;
            case GLFW_KEY_SLASH: return ImGuiKey_Slash;
            case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
            case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
            case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
            case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
            case GLFW_KEY_WORLD_1: return ImGuiKey_Oem102;
            case GLFW_KEY_WORLD_2: return ImGuiKey_Oem102;
            case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
            case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
            case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
            case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
            case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
            case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
            case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
            case GLFW_KEY_KP_0: return ImGuiKey_Keypad0;
            case GLFW_KEY_KP_1: return ImGuiKey_Keypad1;
            case GLFW_KEY_KP_2: return ImGuiKey_Keypad2;
            case GLFW_KEY_KP_3: return ImGuiKey_Keypad3;
            case GLFW_KEY_KP_4: return ImGuiKey_Keypad4;
            case GLFW_KEY_KP_5: return ImGuiKey_Keypad5;
            case GLFW_KEY_KP_6: return ImGuiKey_Keypad6;
            case GLFW_KEY_KP_7: return ImGuiKey_Keypad7;
            case GLFW_KEY_KP_8: return ImGuiKey_Keypad8;
            case GLFW_KEY_KP_9: return ImGuiKey_Keypad9;
            case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
            case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
            case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
            case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
            case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
            case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
            case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
            case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
            case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
            case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
            case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
            case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
            case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
            case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
            case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
            case GLFW_KEY_MENU: return ImGuiKey_Menu;
            case GLFW_KEY_0: return ImGuiKey_0;
            case GLFW_KEY_1: return ImGuiKey_1;
            case GLFW_KEY_2: return ImGuiKey_2;
            case GLFW_KEY_3: return ImGuiKey_3;
            case GLFW_KEY_4: return ImGuiKey_4;
            case GLFW_KEY_5: return ImGuiKey_5;
            case GLFW_KEY_6: return ImGuiKey_6;
            case GLFW_KEY_7: return ImGuiKey_7;
            case GLFW_KEY_8: return ImGuiKey_8;
            case GLFW_KEY_9: return ImGuiKey_9;
            case GLFW_KEY_A: return ImGuiKey_A;
            case GLFW_KEY_B: return ImGuiKey_B;
            case GLFW_KEY_C: return ImGuiKey_C;
            case GLFW_KEY_D: return ImGuiKey_D;
            case GLFW_KEY_E: return ImGuiKey_E;
            case GLFW_KEY_F: return ImGuiKey_F;
            case GLFW_KEY_G: return ImGuiKey_G;
            case GLFW_KEY_H: return ImGuiKey_H;
            case GLFW_KEY_I: return ImGuiKey_I;
            case GLFW_KEY_J: return ImGuiKey_J;
            case GLFW_KEY_K: return ImGuiKey_K;
            case GLFW_KEY_L: return ImGuiKey_L;
            case GLFW_KEY_M: return ImGuiKey_M;
            case GLFW_KEY_N: return ImGuiKey_N;
            case GLFW_KEY_O: return ImGuiKey_O;
            case GLFW_KEY_P: return ImGuiKey_P;
            case GLFW_KEY_Q: return ImGuiKey_Q;
            case GLFW_KEY_R: return ImGuiKey_R;
            case GLFW_KEY_S: return ImGuiKey_S;
            case GLFW_KEY_T: return ImGuiKey_T;
            case GLFW_KEY_U: return ImGuiKey_U;
            case GLFW_KEY_V: return ImGuiKey_V;
            case GLFW_KEY_W: return ImGuiKey_W;
            case GLFW_KEY_X: return ImGuiKey_X;
            case GLFW_KEY_Y: return ImGuiKey_Y;
            case GLFW_KEY_Z: return ImGuiKey_Z;
            case GLFW_KEY_F1: return ImGuiKey_F1;
            case GLFW_KEY_F2: return ImGuiKey_F2;
            case GLFW_KEY_F3: return ImGuiKey_F3;
            case GLFW_KEY_F4: return ImGuiKey_F4;
            case GLFW_KEY_F5: return ImGuiKey_F5;
            case GLFW_KEY_F6: return ImGuiKey_F6;
            case GLFW_KEY_F7: return ImGuiKey_F7;
            case GLFW_KEY_F8: return ImGuiKey_F8;
            case GLFW_KEY_F9: return ImGuiKey_F9;
            case GLFW_KEY_F10: return ImGuiKey_F10;
            case GLFW_KEY_F11: return ImGuiKey_F11;
            case GLFW_KEY_F12: return ImGuiKey_F12;
            case GLFW_KEY_F13: return ImGuiKey_F13;
            case GLFW_KEY_F14: return ImGuiKey_F14;
            case GLFW_KEY_F15: return ImGuiKey_F15;
            case GLFW_KEY_F16: return ImGuiKey_F16;
            case GLFW_KEY_F17: return ImGuiKey_F17;
            case GLFW_KEY_F18: return ImGuiKey_F18;
            case GLFW_KEY_F19: return ImGuiKey_F19;
            case GLFW_KEY_F20: return ImGuiKey_F20;
            case GLFW_KEY_F21: return ImGuiKey_F21;
            case GLFW_KEY_F22: return ImGuiKey_F22;
            case GLFW_KEY_F23: return ImGuiKey_F23;
            case GLFW_KEY_F24: return ImGuiKey_F24;
            default: return ImGuiKey_None;
        }
    }
        
    void EditorUI::OnKeyboardEvent(Spark::WorldContext& context, Spark::Input::KeyboardEvent event)
    {
        using namespace Spark::Input;

        GLFWwindow* window = static_cast<GLFWwindow*>(Service<Window::IWindowSystem>::Get()->GetWindowHandle());

        int key {0};
        switch(event.button)
        {
            case Key::AlphanumericA:
            {
                key = GLFW_KEY_A;
                break;
            }
            case Key::AlphanumericD:
            {
                key = GLFW_KEY_D;
                break;
            }
            case Key::AlphanumericE:
            {
                key = GLFW_KEY_E;
                break;
            }
            case Key::AlphanumericQ:
            {
                key = GLFW_KEY_Q;
                break;
            }
            case Key::AlphanumericR:
            {
                key = GLFW_KEY_R;
                break;
            }
            case Key::AlphanumericS:
            {
                key = GLFW_KEY_S;
                break;
            }
            case Key::AlphanumericW:
            {
                key = GLFW_KEY_W;
                break;
            }
            case Key::Space:
            {
                key = GLFW_KEY_SPACE;
                break;
            }
            default:
                key = GLFW_KEY_UNKNOWN;
        }

        int action {0};
        if (event.state == InputState::Press)
        {
            action = GLFW_PRESS;
        }
        else if (event.state == InputState::Release)
        {
            action = GLFW_RELEASE;
        }
        else if (event.state == InputState::Repeat)
        {
            action = GLFW_REPEAT;
        }

        int mods {0};
        if (event.mode == InputMode::Shift)
        {
            mods |= GLFW_MOD_SHIFT;
        }
        else if (event.mode == InputMode::Control)
        {
            mods |= GLFW_MOD_CONTROL;
        }
        else if (event.mode == InputMode::Alt)
        {
            mods |= GLFW_MOD_ALT;
        }

        ImGuiKey imgui_key = ImGui_ImplGlfw_KeyToImGuiKey(key, 0);
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(imgui_key, (action == GLFW_PRESS));

    }
}