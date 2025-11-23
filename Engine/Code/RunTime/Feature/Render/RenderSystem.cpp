#include "RenderSystem.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <Log/SpdLogSystem.h>

#include <EASTL/any.h>

#include "../Window/IWindowSystem.h"
#include "../UI/UIBaseSystem.h"

#include <thread>

namespace Spark::Render
{

    void RenderSystem::Initialize()
    {
        BusConnect();
    }

    void RenderSystem::ShutDown()
    {
        BusDisconnect();
    }

    void RenderSystem::OnTick(WorldContext& context, float deltaTime)
    {
        //LOG_INFO("[RenderSystem] render tick");
        //std::this_thread::sleep_for(std::chrono::milliseconds(200));



        // ui pass
        int width, height;
        auto size = Service<Window::IWindowSystem>::Get()->GetWindowSize();
        width = size.first;
        height = size.second;

        glViewport(0, 0, width, height);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        eastl::any renderData = Service<UI::UIBaseSystem>::Get()->GetUIRenderData();
        ImDrawData* data = eastl::any_cast<ImDrawData*>(renderData);
        ImGui_ImplOpenGL3_RenderDrawData(data);
    }
}