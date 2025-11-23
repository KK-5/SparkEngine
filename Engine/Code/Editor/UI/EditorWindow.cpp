#include "EditorWindow.h"

#include <Log/SpdLogSystem.h>

#ifdef _WIN32
    #include "../../RunTime/Platform/Windows/Editor/UI/GetNativeWindowHandle.h"
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>


namespace Editor
{
    void EditorWindow::ConfigureWindow()
    {
    #if defined(IMGUI_IMPL_OPENGL_ES2)
        // GL ES 2.0 + GLSL 100 (WebGL 1.0)
        const char* glsl_version = "#version 100";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    #elif defined(IMGUI_IMPL_OPENGL_ES3)
        // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
        const char* glsl_version = "#version 300 es";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    #elif defined(__APPLE__)
        // GL 3.2 + GLSL 150
        const char* glsl_version = "#version 150";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
    #else
        // GL 3.0 + GLSL 130
        const char* glsl_version = "#version 130";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
        //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
    #endif

        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
        glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    }

    void EditorWindow::Initialize()
    {
        using namespace Spark;
        if (!glfwInit()) 
        {
            LOG_ERROR("[EditorWindow] glfwInit failed!");
            return;
        }

        ConfigureWindow();

        m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
        if (!m_window) {
            LOG_ERROR("[EditorWindow] glfwCreateWindow failed!");
            glfwTerminate();
            return;
        }

        glfwMakeContextCurrent(m_window);
        glfwSwapInterval(1);
    }

    void EditorWindow::ShutDown()
    {
        if (m_window) {
            glfwDestroyWindow(m_window);
        }
        glfwTerminate();
    }

    void EditorWindow::PollEvents()
    {
        glfwPollEvents();
    }

    void EditorWindow::SwapBuffer()
    {
        glfwSwapBuffers(m_window);
    }

    eastl::pair<int, int> EditorWindow::GetWindowPos() const
    {
        int x, y;
        glfwGetWindowPos(m_window, &x, &y);
        return eastl::make_pair<int, int>(x, y);
    }

    bool EditorWindow::ShouldClose() const
    {
        return glfwWindowShouldClose(m_window);
    }

    eastl::pair<int, int> EditorWindow::GetWindowSize() const
    {
        return eastl::make_pair<int, int>(m_width, m_height);
    }

    void* EditorWindow::GetNativeHandle() const
    {
        return GetNativeWindowHandle(m_window);
    }

    Spark::Window::WindowBackend EditorWindow::GetWindowBackend() const
    {
        return Spark::Window::WindowBackend::GLFW;
    }

    void* EditorWindow::GetWindowHandle() const
    {
        return m_window;
    }
}