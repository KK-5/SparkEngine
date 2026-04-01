#pragma once

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>

#include <Service/Service.h>
#include <Window/IWindowSystem.h>

class GLFWwindow;

namespace Spark::SandBox
{
    class SimpleGlfwWindow : public Service<Window::IWindowSystem>::Handler
    {
    public:
        explicit SimpleGlfwWindow(int width, int height, eastl::string_view title)
            : m_width(width),
              m_height(height),
              m_title(title) 
        { }

        // ISystem
        void InitInternal() override;
        void ShutdownInternal() override;

        // IWindowSystem
        void SwapBuffer() override;
        void PollEvents() override;
        bool ShouldClose() const override;

        eastl::pair<int, int> GetWindowSize() const override;
        eastl::pair<int, int> GetWindowPos() const override;

        void* GetNativeHandle() const override;
        void* GetWindowHandle() const override;
        Spark::Window::WindowBackend GetWindowBackend() const override;

    private:
        GLFWwindow*     m_window = nullptr;
        int             m_width;
        int             m_height;
        eastl::string   m_title;
    };
}