#include "EditorTheme.h"

#include <Feature/UI/ImGui/SparkImGui.h>

namespace Editor::Theme
{
    namespace
    {
        ImFont* Resolve(Face face)
        {
            switch (face)
            {
            case Face::Bold: return Spark::UI::Fonts::Bold();
            case Face::Mono: return Spark::UI::Fonts::Mono();
            case Face::UI:   break;
            }
            return Spark::UI::Fonts::UI();
        }

        void PushColor(ImGuiCol idx, ImU32 color, int& count)
        {
            ImGui::PushStyleColor(idx, color);
            ++count;
        }

        void PushVar(ImGuiStyleVar idx, float value, int& count)
        {
            ImGui::PushStyleVar(idx, value);
            ++count;
        }

        void PushVar(ImGuiStyleVar idx, const ImVec2& value, int& count)
        {
            ImGui::PushStyleVar(idx, value);
            ++count;
        }
    }

    ScopedFont::ScopedFont(Face face, float size)
    {
        // A null font means "keep the current one" -- the case before the UI system has
        // loaded anything, where the size is still worth applying.
        ImGui::PushFont(Resolve(face), Px(size));
    }

    ScopedFont::~ScopedFont()
    {
        ImGui::PopFont();
    }

    Scoped::Scoped()
    {
        PushColor(ImGuiCol_WindowBg, kWindowBg, m_colors);
        PushColor(ImGuiCol_PopupBg,  kBlockBg,  m_colors);
        // Panels fill their own backgrounds, over the window's.
        PushColor(ImGuiCol_ChildBg,  IM_COL32(0, 0, 0, 0), m_colors);

        // One colour has to serve both the window's border and every input frame's. The
        // window's is the one that shows against the desktop, so it wins; the frames come
        // out four greys lighter than the mockup, which is not a difference you can see.
        PushColor(ImGuiCol_Border,    kBorderWindow, m_colors);
        PushColor(ImGuiCol_Separator, kBorderInner,  m_colors);

        PushColor(ImGuiCol_Text,         kText,    m_colors);
        PushColor(ImGuiCol_TextDisabled, kTextDim, m_colors);

        PushColor(ImGuiCol_FrameBg,        kFrameBg,                         m_colors);
        PushColor(ImGuiCol_FrameBgHovered, IM_COL32(0x16, 0x19, 0x1D, 0xFF), m_colors);
        PushColor(ImGuiCol_FrameBgActive,  IM_COL32(0x1B, 0x1E, 0x23, 0xFF), m_colors);

        PushColor(ImGuiCol_Button,        kButton,    m_colors);
        PushColor(ImGuiCol_ButtonHovered, kButtonHov, m_colors);
        PushColor(ImGuiCol_ButtonActive,  kButtonHov, m_colors);

        PushColor(ImGuiCol_Header,        kButton,    m_colors);
        PushColor(ImGuiCol_HeaderHovered, kButtonHov, m_colors);
        PushColor(ImGuiCol_HeaderActive,  kButtonHov, m_colors);

        PushColor(ImGuiCol_SliderGrab,       kAccent,    m_colors);
        PushColor(ImGuiCol_SliderGrabActive, kAccentHov, m_colors);
        PushColor(ImGuiCol_CheckMark,        kAccent,    m_colors);

        PushColor(ImGuiCol_ScrollbarBg,          IM_COL32(0, 0, 0, 0), m_colors);
        PushColor(ImGuiCol_ScrollbarGrab,        kScrollbar,           m_colors);
        PushColor(ImGuiCol_ScrollbarGrabHovered, kButtonHov,           m_colors);
        PushColor(ImGuiCol_ScrollbarGrabActive,  kButtonHov,           m_colors);

        // Zero window padding: the title bar, the tab strip and the footer span the full
        // width, and each block indents its own contents. Anything the window itself padded
        // would push those edges inwards.
        PushVar(ImGuiStyleVar_WindowPadding,     ImVec2(0.f, 0.f),         m_vars);
        PushVar(ImGuiStyleVar_WindowRounding,    Px(6.f),                  m_vars);
        PushVar(ImGuiStyleVar_WindowBorderSize,  1.f,                      m_vars);
        PushVar(ImGuiStyleVar_ChildRounding,     0.f,                      m_vars);
        PushVar(ImGuiStyleVar_ChildBorderSize,   0.f,                      m_vars);
        PushVar(ImGuiStyleVar_FrameRounding,     Px(3.f),                  m_vars);
        PushVar(ImGuiStyleVar_FrameBorderSize,   1.f,                      m_vars);
        PushVar(ImGuiStyleVar_FramePadding,      ImVec2(Px(7.f), Px(4.f)), m_vars);
        PushVar(ImGuiStyleVar_ItemSpacing,       ImVec2(Px(7.f), Px(6.f)), m_vars);
        PushVar(ImGuiStyleVar_GrabRounding,      Px(2.f),                  m_vars);
        PushVar(ImGuiStyleVar_PopupRounding,     Px(3.f),                  m_vars);
        PushVar(ImGuiStyleVar_ScrollbarRounding, Px(5.f),                  m_vars);
        PushVar(ImGuiStyleVar_ScrollbarSize,     Px(9.f),                  m_vars);
    }

    Scoped::~Scoped()
    {
        ImGui::PopStyleVar(m_vars);
        ImGui::PopStyleColor(m_colors);
    }
}
