#pragma once

#include <GLFW/glfw3.h>
#include <EASTL/string.h>

#include <Service/Service.h>

#include <Window/IWindowSystem.h>

namespace Spark::Window
{
    class GlfwWindow final : public Service<IWindowSystem>::Handler
    {
    public:
        GlfwWindow(int width, int height, eastl::string_view title)
            : m_width(width), m_height(height), m_title(title) 
        { 

        }

        // ISystem
        void InitInternal() override;
        void ShutdownInternal() override;

        // IWindowSystem
        void SwapBuffer() override;
        void PollEvents() override;
        bool ShouldClose() const override;

        Math::Vector2Int GetWindowSize() override;
        Math::Vector2Int GetWindowPos() override;

        void* GetNativeHandle() const override;
        void* GetWindowHandle() const override;
        Spark::Window::WindowBackend GetWindowBackend() const override;

    private:
        void ConfigureWindow();
        
    private:
        GLFWwindow*     m_window;
        int             m_width;
        int             m_height;
        eastl::string   m_title;
    };
}