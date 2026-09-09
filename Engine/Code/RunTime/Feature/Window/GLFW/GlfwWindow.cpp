
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

        // glfw places a new window at an arbitrary corner, and a size larger than the
        // monitor stays larger than the monitor. Both are what SetWindowSize corrects.
        SetWindowSize({m_width, m_height});
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

    void GlfwWindow::SetWindowSize(const Math::Vector2Int& size)
    {
        if (!m_window || size.x <= 0 || size.y <= 0)
        {
            return;
        }

        int width  = size.x;
        int height = size.y;

        // Both glfw calls below deal in the client area, so the frame has to come out of
        // the budget for the whole window to fit and for centring to measure the window.
        int frameLeft = 0, frameTop = 0, frameRight = 0, frameBottom = 0;
        glfwGetWindowFrameSize(m_window, &frameLeft, &frameTop, &frameRight, &frameBottom);

        int workX = 0, workY = 0, workWidth = 0, workHeight = 0;
        if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
        {
            glfwGetMonitorWorkarea(monitor, &workX, &workY, &workWidth, &workHeight);
        }

        const int budgetWidth  = workWidth - frameLeft - frameRight;
        const int budgetHeight = workHeight - frameTop - frameBottom;

        if (budgetWidth > 0 && budgetHeight > 0)
        {
            width  = (width > budgetWidth) ? budgetWidth : width;
            height = (height > budgetHeight) ? budgetHeight : height;
        }

        glfwSetWindowSize(m_window, width, height);

        if (budgetWidth > 0 && budgetHeight > 0)
        {
            glfwSetWindowPos(m_window,
                workX + frameLeft + (budgetWidth - width) / 2,
                workY + frameTop + (budgetHeight - height) / 2);
        }

        m_width  = width;
        m_height = height;
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