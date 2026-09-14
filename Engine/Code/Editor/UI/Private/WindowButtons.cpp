#include "WindowButtons.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>

#include <Service/Service.h>
#include <Feature/Window/IWindowSystem.h>

#include "EditorTheme.h"

namespace Editor
{
    using namespace Spark;

    namespace
    {
        constexpr float kButtonWidth = Theme::Px(34.f);
        constexpr float kGlyph       = Theme::Px(7.f);

        enum class Button
        {
            Minimize,
            Maximize,
            Restore,
            Close,
        };

        void DrawGlyph(ImDrawList* draw, Button button, ImVec2 center, ImU32 color)
        {
            // Snapped to the pixel centre, or the 1px strokes blur across two rows.
            center = ImVec2(ImFloor(center.x) + 0.5f, ImFloor(center.y) + 0.5f);
            const float h = ImFloor(kGlyph * 0.5f);

            switch (button)
            {
            case Button::Minimize:
                draw->AddLine(ImVec2(center.x - h, center.y), ImVec2(center.x + h + 1.f, center.y), color);
                break;
            case Button::Maximize:
                draw->AddRect(ImVec2(center.x - h, center.y - h), ImVec2(center.x + h, center.y + h), color);
                break;
            case Button::Restore:
            {
                // A front square, and the corner of the one behind it showing above and right.
                const float o = ImFloor(h * 0.5f);
                draw->AddRect(ImVec2(center.x - h, center.y - h + o), ImVec2(center.x + h - o, center.y + h), color);
                draw->AddLine(ImVec2(center.x - h + o, center.y - h + o), ImVec2(center.x - h + o, center.y - h), color);
                draw->AddLine(ImVec2(center.x - h + o, center.y - h), ImVec2(center.x + h, center.y - h), color);
                draw->AddLine(ImVec2(center.x + h, center.y - h), ImVec2(center.x + h, center.y + h - o), color);
                draw->AddLine(ImVec2(center.x + h, center.y + h - o), ImVec2(center.x + h - o, center.y + h - o), color);
                break;
            }
            case Button::Close:
                draw->AddLine(ImVec2(center.x - h, center.y - h), ImVec2(center.x + h + 1.f, center.y + h + 1.f), color);
                draw->AddLine(ImVec2(center.x + h, center.y - h), ImVec2(center.x - h - 1.f, center.y + h + 1.f), color);
                break;
            }
        }
    }

    float WindowButtonsWidth(bool maximizable)
    {
        return kButtonWidth * (maximizable ? 3.f : 2.f);
    }

    void DrawWindowButtons(float right, float top, float height, bool maximizable)
    {
        auto* windowSystem = Service<Window::IWindowSystem>::Get();
        auto* window = windowSystem ? static_cast<GLFWwindow*>(windowSystem->GetWindowHandle()) : nullptr;
        if (!window)
        {
            return;
        }

        Button buttons[3];
        int    count = 0;
        buttons[count++] = Button::Minimize;
        if (maximizable)
        {
            const bool maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
            buttons[count++] = maximized ? Button::Restore : Button::Maximize;
        }
        buttons[count++] = Button::Close;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float left = right - WindowButtonsWidth(maximizable);
        for (int i = 0; i < count; ++i)
        {
            const Button button = buttons[i];
            const ImVec2 min(left + kButtonWidth * static_cast<float>(i), top);
            const ImVec2 max(min.x + kButtonWidth, top + height);

            ImGui::SetCursorScreenPos(min);
            ImGui::PushID(static_cast<int>(button));
            const bool pressed = ImGui::InvisibleButton("##WindowButton", ImVec2(kButtonWidth, height));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            const bool close = (button == Button::Close);
            if (hovered)
            {
                draw->AddRectFilled(min, max, close ? Theme::kCloseHovBg : Theme::kButtonHov);
            }
            ImU32 glyph = Theme::kTextDim;
            if (hovered)
            {
                glyph = close ? Theme::kCloseHovText : Theme::kTextStrong;
            }
            DrawGlyph(draw, button, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), glyph);

            if (!pressed)
            {
                continue;
            }
            switch (button)
            {
            case Button::Minimize: glfwIconifyWindow(window);                   break;
            case Button::Maximize: glfwMaximizeWindow(window);                  break;
            case Button::Restore:  glfwRestoreWindow(window);                   break;
            case Button::Close:    glfwSetWindowShouldClose(window, GLFW_TRUE); break;
            }
        }
    }
}
