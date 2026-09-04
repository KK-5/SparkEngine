#include "SparkImGui.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>

#include <Window/IWindowSystem.h>
#include <Render/Feature/UI/RenderUIInterface.h>

namespace Spark::UI
{
    namespace
    {
        ImFont* s_uiFont   = nullptr;
        ImFont* s_boldFont = nullptr;
        ImFont* s_monoFont = nullptr;

        //! The size the atlas registers each face at. Only the default font's size is really
        //! a decision -- it is what every panel that never calls PushFont ends up drawing at.
        //! 15 is the mockup's 12px at the 125% the editor is designed for.
        constexpr float kBaseFontSize = 15.0f;
    }

    namespace Fonts
    {
        ImFont* UI()   { return s_uiFont; }
        ImFont* Bold() { return s_boldFont; }
        ImFont* Mono() { return s_monoFont; }
    }

    void SparkImGui::InitInternal()
    {
        UIBaseSystem::InitInternal();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        // Keyboard navigation stays on, but it no longer claims the keyboard just for being
        // active. Left at its default, io.NavActive is true whenever any imgui window holds
        // focus -- which is always -- so io.WantCaptureKeyboard would be permanently true and
        // nothing below the UI could ever see a key. Off, it is true only while a widget is
        // actually being edited, which is what "the UI needs this key" should mean.
        io.ConfigNavCaptureKeyboard = false;
        // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        // Barlow first, so it is Fonts[0] -- the font every panel that never calls PushFont
        // draws with. Bold and mono are additions, reached through Fonts::.
        s_uiFont   = io.Fonts->AddFontFromFileTTF(SPARK_UI_FONT_DIR "/Barlow-Regular.ttf", kBaseFontSize);
        s_boldFont = io.Fonts->AddFontFromFileTTF(SPARK_UI_FONT_DIR "/Barlow-SemiBold.ttf", kBaseFontSize);
        s_monoFont = io.Fonts->AddFontFromFileTTF(SPARK_UI_FONT_DIR "/JetBrainsMono-Regular.ttf", kBaseFontSize);

        ImGui::StyleColorsDark();
    
        ImGuiStyle& style = ImGui::GetStyle();
        /*
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.WindowPadding = ImVec2(0.0f, 0.0f);
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
        */

        ASSERT(Service<Window::IWindowSystem>::Get(), "[SparkImGui] IWindowSystem is invalid.");
        ASSERT(Service<Render::RenderUIInterface>::Get(), "[SparkImGui] RenderUIInterface is invaild.");

        if (Service<Window::IWindowSystem>::Get()->GetWindowBackend() == Window::WindowBackend::GLFW)
        {
            GLFWwindow* window = static_cast<GLFWwindow*>(Service<Window::IWindowSystem>::Get()->GetWindowHandle());
            ImGui_ImplGlfw_InitForOther(window, true);
        }

        Service<Render::RenderUIInterface>::Get()->Init();
    };

    void SparkImGui::ShutdownInternal()
    {
        ASSERT(Service<Window::IWindowSystem>::Get(), "[SparkImGui] IWindowSystem is invalid.");
        ASSERT(Service<Render::RenderUIInterface>::Get(), "[SparkImGui] RenderUIInterface is invaild.");

        Service<Render::RenderUIInterface>::Get()->Shutdown();

        if (Service<Window::IWindowSystem>::Get()->GetWindowBackend() == Window::WindowBackend::GLFW)
        {
            ImGui_ImplGlfw_Shutdown();
        }

        ImGui::DestroyContext();
    }

    void SparkImGui::NewFrame()
    {
        Service<Render::RenderUIInterface>::Get()->NewFrame();
        if (Service<Window::IWindowSystem>::Get()->GetWindowBackend() == Window::WindowBackend::GLFW)
        {
            ImGui_ImplGlfw_NewFrame();
        }
        ImGui::NewFrame();
    }

    void SparkImGui::EndFrame()
    {
    }

    eastl::any SparkImGui::RenderUI()
    {
        ImGui::Render();
        return ImGui::GetDrawData();
    }

    bool SparkImGui::WantCaptureMouse() const
    {
        return ImGui::GetIO().WantCaptureMouse;
    }

    bool SparkImGui::WantCaptureKeyboard() const
    {
        return ImGui::GetIO().WantCaptureKeyboard;
    }
}