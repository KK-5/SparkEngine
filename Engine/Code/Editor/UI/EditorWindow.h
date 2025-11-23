#pragma once

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>

#include <Service/Service.h>

#include <Feature/Window/IWindowSystem.h>

class GLFWwindow;

namespace Editor
{
    class EditorWindow final : public Spark::Service<Spark::Window::IWindowSystem>::Handler
    {
    public:
        EditorWindow(int width, int height, eastl::string_view title)
            : m_width(width),
              m_height(height),
              m_title(title) 
        { }

        // ISystem
        void Initialize() override;
        void ShutDown() override;

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
        void ConfigureWindow();

    private:
        GLFWwindow*     m_window;
        int             m_width;
        int             m_height;
        eastl::string   m_title;
    };
}