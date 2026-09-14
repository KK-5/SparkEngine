#pragma once

#include <Math/Vector2.h>

namespace Editor
{
    //! Replaces the OS title bar with the editor's own, keeping native drag, snap and resize.
    //!
    //! Only WM_NCCALCSIZE and WM_NCHITTEST are handled; every click that lands on the client
    //! area still reaches GLFW and InputEventBus as before. A no-op off Windows.
    class WindowChrome final
    {
    public:
        //! Client pixels. The OS drags the window from [m_dragMinX, m_dragMaxX) above m_height.
        struct Caption
        {
            float m_height   = 0.f;
            float m_dragMinX = 0.f;
            float m_dragMaxX = 0.f;
        };

        ~WindowChrome();

        //! `nativeHandle` is the HWND. Safe to call again once installed.
        void Install(void* nativeHandle);
        void Uninstall();
        bool IsInstalled() const { return m_hwnd != nullptr; }

        //! Read by the hit test, which runs between frames on the same thread.
        void SetCaption(const Caption& caption) { m_caption = caption; }
        const Caption& GetCaption() const { return m_caption; }

        //! What to pass IWindowSystem::SetWindowSize for a client area of `clientSize`. It
        //! still budgets for the OS title bar, which is client area once installed.
        Spark::Math::Vector2Int WindowSizeFor(const Spark::Math::Vector2Int& clientSize) const;

    private:
        void*   m_hwnd = nullptr;
        Caption m_caption;
    };
}
