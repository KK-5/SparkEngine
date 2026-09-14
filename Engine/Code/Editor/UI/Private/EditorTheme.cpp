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

    void Apply()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        const auto set = [&style](ImGuiCol idx, ImU32 color)
        {
            style.Colors[idx] = ImGui::ColorConvertU32ToFloat4(color);
        };
        constexpr ImU32 kClear = IM_COL32(0, 0, 0, 0);

        set(ImGuiCol_Text,         kText);
        set(ImGuiCol_TextDisabled, kTextDim);
        set(ImGuiCol_WindowBg,     kWindowBg);
        set(ImGuiCol_ChildBg,      kClear);
        set(ImGuiCol_PopupBg,      kTitleBg);
        // Docked panels and input frames share this; floating windows take kBorderWindow
        // through Scoped.
        set(ImGuiCol_Border,       kFrameBorder);
        set(ImGuiCol_BorderShadow, kClear);

        set(ImGuiCol_FrameBg,        kFrameBg);
        set(ImGuiCol_FrameBgHovered, kFrameBgHov);
        set(ImGuiCol_FrameBgActive,  kFrameBgAct);

        set(ImGuiCol_TitleBg,          kFooterBg);
        set(ImGuiCol_TitleBgActive,    kFooterBg);
        set(ImGuiCol_TitleBgCollapsed, kFooterBg);
        set(ImGuiCol_MenuBarBg,        kFooterBg);

        set(ImGuiCol_ScrollbarBg,          kClear);
        set(ImGuiCol_ScrollbarGrab,        kScrollbar);
        set(ImGuiCol_ScrollbarGrabHovered, kButtonHov);
        set(ImGuiCol_ScrollbarGrabActive,  kButtonHov);

        set(ImGuiCol_CheckMark,        kAccent);
        set(ImGuiCol_SliderGrab,       kAccent);
        set(ImGuiCol_SliderGrabActive, kAccentHov);

        set(ImGuiCol_Button,        kButton);
        set(ImGuiCol_ButtonHovered, kButtonHov);
        set(ImGuiCol_ButtonActive,  kButtonHov);

        // Header is what a selected tree row or selectable fills with; hover is also what a
        // menu item lights up with.
        set(ImGuiCol_Header,        kSelection);
        set(ImGuiCol_HeaderHovered, kButtonHov);
        set(ImGuiCol_HeaderActive,  kSelection);

        set(ImGuiCol_Separator,        kBorderInner);
        set(ImGuiCol_SeparatorHovered, kAccent);
        set(ImGuiCol_SeparatorActive,  kAccentHov);

        set(ImGuiCol_ResizeGrip,        kClear);
        set(ImGuiCol_ResizeGripHovered, kButtonHov);
        set(ImGuiCol_ResizeGripActive,  kAccent);

        set(ImGuiCol_InputTextCursor, kTextStrong);

        set(ImGuiCol_Tab,                       kFooterBg);
        set(ImGuiCol_TabHovered,                kButtonHov);
        set(ImGuiCol_TabSelected,               kWindowBg);
        set(ImGuiCol_TabSelectedOverline,       kAccent);
        set(ImGuiCol_TabDimmed,                 kFooterBg);
        set(ImGuiCol_TabDimmedSelected,         kWindowBg);
        set(ImGuiCol_TabDimmedSelectedOverline, kClear);

        set(ImGuiCol_DockingPreview, IM_COL32(0x7F, 0xD6, 0xC2, 0x40));
        set(ImGuiCol_DockingEmptyBg, kAppBg);

        set(ImGuiCol_PlotLines,            kTextDim);
        set(ImGuiCol_PlotLinesHovered,     kAccent);
        set(ImGuiCol_PlotHistogram,        kAccent);
        set(ImGuiCol_PlotHistogramHovered, kAccentHov);

        set(ImGuiCol_TableHeaderBg,     kFooterBg);
        set(ImGuiCol_TableBorderStrong, kBorderPanel);
        set(ImGuiCol_TableBorderLight,  kBorderInner);
        set(ImGuiCol_TableRowBg,        kClear);
        set(ImGuiCol_TableRowBgAlt,     IM_COL32(0xFF, 0xFF, 0xFF, 0x05));

        set(ImGuiCol_TextLink,       kAccent);
        set(ImGuiCol_TextSelectedBg, IM_COL32(0x7F, 0xD6, 0xC2, 0x40));
        set(ImGuiCol_TreeLines,      kDivider);
        set(ImGuiCol_DragDropTarget, kAccent);
        set(ImGuiCol_UnsavedMarker,  kDirty);

        set(ImGuiCol_NavCursor,             kAccent);
        set(ImGuiCol_NavWindowingHighlight, kAccent);
        set(ImGuiCol_NavWindowingDimBg,     kModalDim);
        set(ImGuiCol_ModalWindowDimBg,      kModalDim);

        // Before the first NewFrame, so it is taken as-is rather than via _NextFrameFontSizeBase.
        style.FontSizeBase = Px(kSizeBody);

        style.WindowPadding            = ImVec2(Px(8.f), Px(8.f));
        style.WindowRounding           = Px(4.f);
        style.WindowBorderSize         = 1.f;
        style.WindowMenuButtonPosition = ImGuiDir_None;
        style.ChildRounding            = 0.f;
        style.ChildBorderSize          = 0.f;
        style.PopupRounding            = Px(4.f);
        style.PopupBorderSize          = 1.f;
        style.FramePadding             = ImVec2(Px(7.f), Px(4.f));
        style.FrameRounding            = Px(3.f);
        style.FrameBorderSize          = 1.f;
        style.ItemSpacing              = ImVec2(Px(7.f), Px(6.f));
        style.ItemInnerSpacing         = ImVec2(Px(4.f), Px(4.f));
        style.CellPadding              = ImVec2(Px(4.f), Px(2.f));
        style.IndentSpacing            = Px(14.f);
        style.ScrollbarSize            = Px(9.f);
        style.ScrollbarRounding        = Px(5.f);
        style.GrabMinSize              = Px(10.f);
        style.GrabRounding             = Px(2.f);
        style.TabRounding              = 0.f;
        style.TabBorderSize            = 0.f;
        style.TabBarBorderSize         = 1.f;
        style.TabBarOverlineSize       = Px(2.f);
        style.TreeLinesSize            = 1.f;
        style.DockingSeparatorSize     = Px(4.f);
    }

    Scoped::Scoped()
    {
        // A combo's list inside the window is a raised block, not a menu.
        PushColor(ImGuiCol_PopupBg, kBlockBg,      m_colors);
        PushColor(ImGuiCol_Border,  kBorderWindow, m_colors);
        PushColor(ImGuiCol_Header,       kButton,    m_colors);
        PushColor(ImGuiCol_HeaderActive, kButtonHov, m_colors);

        // Zero window padding: the title bar, the tab strip and the footer span the full
        // width, and each block indents its own contents.
        PushVar(ImGuiStyleVar_WindowPadding,  ImVec2(0.f, 0.f), m_vars);
        PushVar(ImGuiStyleVar_WindowRounding, Px(6.f),          m_vars);
    }

    Scoped::~Scoped()
    {
        ImGui::PopStyleVar(m_vars);
        ImGui::PopStyleColor(m_colors);
    }
}
