#include "SparkImGui.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>

#include <Window/IWindowSystem.h>
#include <Render/Feature/UI/RenderUIInterface.h>

namespace Spark::UI
{
    void SparkImGui::InitInternal()
    {
        UIBaseSystem::InitInternal();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.FontGlobalScale = 1.2f;

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
        Service<Window::IWindowSystem>::Get()->SwapBuffer();
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