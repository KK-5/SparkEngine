#include "WindowChrome.h"

#ifdef _WIN32
    #include <windows.h>
    #include <windowsx.h>
    #include <commctrl.h>
#endif

namespace Editor
{
#ifdef _WIN32
    namespace
    {
        constexpr UINT_PTR kSubclassId = 0x53504B43;   // 'SPKC'

        //! The resize border the OS would have drawn, padding included.
        SIZE FrameThickness(HWND hwnd)
        {
            const UINT dpi    = GetDpiForWindow(hwnd);
            const int  padded = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            return {GetSystemMetricsForDpi(SM_CXFRAME, dpi) + padded,
                    GetSystemMetricsForDpi(SM_CYFRAME, dpi) + padded};
        }

        LRESULT CalcSize(HWND hwnd, WPARAM wParam, LPARAM lParam)
        {
            if (wParam != TRUE)
            {
                return DefSubclassProc(hwnd, WM_NCCALCSIZE, wParam, lParam);
            }

            // Let the OS keep the side and bottom borders, then take the top back: that is
            // the title bar and the top border.
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            const LONG top = params->rgrc[0].top;

            const LRESULT result = DefSubclassProc(hwnd, WM_NCCALCSIZE, wParam, lParam);
            if (result != 0)
            {
                return result;
            }

            params->rgrc[0].top = top;
            // A maximized window overhangs the monitor by its frame; without this the bar
            // would be cut off.
            if (IsZoomed(hwnd))
            {
                params->rgrc[0].top += FrameThickness(hwnd).cy;
            }
            return 0;
        }

        LRESULT HitTest(HWND hwnd, WPARAM wParam, LPARAM lParam, const WindowChrome::Caption& caption)
        {
            // Side and bottom borders are still the OS's.
            const LRESULT hit = DefSubclassProc(hwnd, WM_NCHITTEST, wParam, lParam);
            if (hit != HTCLIENT)
            {
                return hit;
            }

            POINT point {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &point);

            // The top border became client area, so its resize handle is ours to report.
            const bool resizable = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0;
            if (resizable && !IsZoomed(hwnd))
            {
                const SIZE frame = FrameThickness(hwnd);
                if (point.y < frame.cy)
                {
                    RECT client;
                    GetClientRect(hwnd, &client);
                    if (point.x < frame.cx)
                    {
                        return HTTOPLEFT;
                    }
                    if (point.x >= client.right - frame.cx)
                    {
                        return HTTOPRIGHT;
                    }
                    return HTTOP;
                }
            }

            const float x = static_cast<float>(point.x);
            const float y = static_cast<float>(point.y);
            if (y < caption.m_height && x >= caption.m_dragMinX && x < caption.m_dragMaxX)
            {
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        LRESULT CALLBACK ChromeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR, DWORD_PTR refData)
        {
            const auto* chrome = reinterpret_cast<const WindowChrome*>(refData);
            switch (msg)
            {
            case WM_NCCALCSIZE:
                return CalcSize(hwnd, wParam, lParam);
            case WM_NCHITTEST:
                return HitTest(hwnd, wParam, lParam, chrome->GetCaption());
            case WM_NCDESTROY:
                RemoveWindowSubclass(hwnd, ChromeProc, kSubclassId);
                break;
            default:
                break;
            }
            return DefSubclassProc(hwnd, msg, wParam, lParam);
        }

        //! Makes the OS re-ask WM_NCCALCSIZE now rather than at the next resize.
        void RefreshFrame(HWND hwnd)
        {
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    WindowChrome::~WindowChrome()
    {
        Uninstall();
    }

    void WindowChrome::Install(void* nativeHandle)
    {
        const HWND hwnd = static_cast<HWND>(nativeHandle);
        if (m_hwnd || !hwnd)
        {
            return;
        }

        if (!SetWindowSubclass(hwnd, ChromeProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this)))
        {
            return;
        }
        m_hwnd = hwnd;
        RefreshFrame(hwnd);
    }

    void WindowChrome::Uninstall()
    {
        const HWND hwnd = static_cast<HWND>(m_hwnd);
        m_hwnd = nullptr;
        if (!hwnd || !IsWindow(hwnd))
        {
            return;
        }

        RemoveWindowSubclass(hwnd, ChromeProc, kSubclassId);
        RefreshFrame(hwnd);
    }

    Spark::Math::Vector2Int WindowChrome::WindowSizeFor(const Spark::Math::Vector2Int& clientSize) const
    {
        const HWND hwnd = static_cast<HWND>(m_hwnd);
        if (!hwnd)
        {
            return clientSize;
        }

        // The same frame GLFW adds when it turns a client size into a window size; its top is
        // what CalcSize handed back to the client.
        RECT frame {0, 0, 0, 0};
        AdjustWindowRectExForDpi(&frame,
                                 static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)),
                                 GetDpiForWindow(hwnd));
        return {clientSize.x, clientSize.y + frame.top};
    }
#else
    WindowChrome::~WindowChrome() = default;

    void WindowChrome::Install(void*) {}
    void WindowChrome::Uninstall() {}

    Spark::Math::Vector2Int WindowChrome::WindowSizeFor(const Spark::Math::Vector2Int& clientSize) const
    {
        return clientSize;
    }
#endif
}
