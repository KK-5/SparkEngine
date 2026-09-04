
#include "GlfwWindow.h"

#include <Log/ILogSystem.h>

#ifdef _WIN32
    #include <Windows/Editor/UI/GetNativeWindowHandle.h>
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace Spark::Window
{
    void GlfwWindow::InitInternal()
    {
        using namespace Spark;
        if (!glfwInit()) 
        {
            LOG_ERROR("[GlfwWindow] glfwInit failed!");
            return;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
        if (!m_window) {
            LOG_ERROR("[GlfwWindow] glfwCreateWindow failed!");
            glfwTerminate();
            return;
        }
    }

    void GlfwWindow::ShutdownInternal()
    {
        if (m_window) {
            glfwDestroyWindow(m_window);
        }
        glfwTerminate();
    }

    void GlfwWindow::PollEvents()
    {
        glfwPollEvents();
    }

    void GlfwWindow::SwapBuffer()
    {
        glfwSwapBuffers(m_window);
    }

    Math::Vector2Int GlfwWindow::GetWindowPos()
    {
        int x, y;
        glfwGetWindowPos(m_window, &x, &y);
        return { x, y };
    }

    bool GlfwWindow::ShouldClose() const
    {
        return glfwWindowShouldClose(m_window);
    }

    Math::Vector2Int GlfwWindow::GetWindowSize()
    {
        glfwGetWindowSize(m_window, &m_width, &m_height);
        return { m_width, m_height };
    }

    void* GlfwWindow::GetNativeHandle() const
    {
        return GetNativeWindowHandle(m_window);
    }

    Spark::Window::WindowBackend GlfwWindow::GetWindowBackend() const
    {
        return Spark::Window::WindowBackend::GLFW;
    }

    void* GlfwWindow::GetWindowHandle() const
    {
        return m_window;
    }
}