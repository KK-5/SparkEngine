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

        ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
    }

    void EditorUI::OnMouseCursorPosEvent(Spark::WorldContext& context, Spark::Input::MouseCursorPosEvent event)
    {
        GLFWwindow* window = static_cast<GLFWwindow*>(Service<Window::IWindowSystem>::Get()->GetWindowHandle());

        ImGui_ImplGlfw_CursorPosCallback(window, (double)event.xPos, (double)event.yPos);
    }

    void EditorUI::OnMouseScrollEvent(Spark::WorldContext& context, Spark::Input::MouseScrollEvent event)
    {
        GLFWwindow* window = static_cast<GLFWwindow*>(Service<Window::IWindowSystem>::Get()->GetWindowHandle());

        ImGui_ImplGlfw_ScrollCallback(window, (double)event.xOffset, (double)event.yOffset);
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

        ImGui_ImplGlfw_KeyCallback(window, key, 0, action, mods);
    }
}