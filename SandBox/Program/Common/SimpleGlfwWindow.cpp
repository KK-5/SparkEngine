#include "SimpleGlfwWindow.h"

#include <Log/ILogSystem.h>

#ifdef _WIN32
    #include "../../Code/RunTime/Platform/Windows/Editor/UI/GetNativeWindowHandle.h"
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>


namespace Spark::SandBox
{
    void SimpleGlfwWindow::InitInternal()
    {
        if (!glfwInit()) 
        {
            LOG_ERROR("[SimpleGlfwWindow] glfwInit failed!");
            return;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
        if (!m_window) {
            LOG_ERROR("[SimpleGlfwWindow] glfwCreateWindow failed!");
            glfwTerminate();
            return;
        }
    }

    void SimpleGlfwWindow::ShutdownInternal()
    {
        if (m_window) {
            glfwDestroyWindow(m_window);
        }
        glfwTerminate();
        m_window = nullptr;
    }

    void SimpleGlfwWindow::PollEvents()
    {
        glfwPollEvents();
    }

    void SimpleGlfwWindow::SwapBuffer()
    {
        glfwSwapBuffers(m_window);
    }

    Math::Vector2Int SimpleGlfwWindow::GetWindowPos()
    {
        int x, y;
        glfwGetWindowPos(m_window, &x, &y);
        return { x, y };
    }

    bool SimpleGlfwWindow::ShouldClose() const
    {
        return glfwWindowShouldClose(m_window);
    }

    Math::Vector2Int SimpleGlfwWindow::GetWindowSize()
    {
        glfwGetWindowSize(m_window, &m_width, &m_height);
        return { m_width, m_height };
    }

    void* SimpleGlfwWindow::GetNativeHandle() const
    {
        return GetNativeWindowHandle(m_window);
    }

    Spark::Window::WindowBackend SimpleGlfwWindow::GetWindowBackend() const
    {
        return Spark::Window::WindowBackend::GLFW;
    }

    void* SimpleGlfwWindow::GetWindowHandle() const
    {
        return m_window;
    }
}