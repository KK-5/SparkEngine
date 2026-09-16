#include "Toolbar.h"

#include <cstdio>

#include <imgui.h>
#include <imgui_internal.h>

#include "EditorTheme.h"

namespace Editor
{
    namespace
    {
        constexpr float kHeight   = Theme::Px(38.f);
        constexpr float kPad      = Theme::Px(10.f);
        constexpr float kGap      = Theme::Px(14.f);
        constexpr float kItemH    = Theme::Px(26.f);
        constexpr float kItemPadX = Theme::Px(9.f);
        constexpr float kChipPadX = Theme::Px(11.f);
        constexpr float kRounding = Theme::Px(4.f);
        constexpr float kWellPad  = Theme::Px(2.f);

        //! Concentric with the well around it: the outer radius less the padding between.
        constexpr float kInnerRounding = kRounding - kWellPad;

        float LabelWidth(const char* label)
        {
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);
            return ImGui::CalcTextSize(label).x;
        }

        //! Centred in a box, under whatever face the caller pushed.
        void CenteredText(ImDrawList* draw, const ImVec2& min, const ImVec2& max, ImU32 color,
                          const char* label)
        {
            const ImVec2 size = ImGui::CalcTextSize(label);
            draw->AddText(Theme::Snap(ImVec2((min.x + max.x - size.x) * 0.5f,
                                             (min.y + max.y - ImGui::GetFontSize()) * 0.5f)),
                          color, label);
        }

        //! What a control needs to know about the cursor. `m_lit` is the eased hover, and
        //! `m_held` is the frame-accurate press -- a click must not wait for a fade.
        struct State
        {
            bool  m_pressed = false;
            bool  m_held    = false;
            float m_lit     = 0.f;
            ImGuiID m_id    = 0;
        };

        //! A hit box with no drawing of its own. `id` is unique within the toolbar.
        State Hit(const char* id, const ImVec2& min, const ImVec2& max, const char* tooltip)
        {
            State state;

            ImGui::SetCursorScreenPos(min);
            state.m_pressed = ImGui::InvisibleButton(id, ImVec2(max.x - min.x, max.y - min.y));
            state.m_held    = ImGui::IsItemActive();
            state.m_id      = ImGui::GetItemID();

            const bool hovered = ImGui::IsItemHovered();
            state.m_lit = Theme::Fade(state.m_id, hovered || state.m_held);

            // Delayed, or the row flashes a box at every cursor that crosses it. The colours
            // are pushed here because a tooltip is a plain window, and the host window has
            // WindowBg pushed to transparent.
            if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::kTitleBg);
                ImGui::PushStyleColor(ImGuiCol_Border, Theme::kBorderPopup);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextItem);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                    ImVec2(Theme::Px(9.f), Theme::Px(6.f)));
                ImGui::SetTooltip("%s", tooltip);
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
            }
            return state;
        }

        //! One choice of a segmented group -- the accent fills it when it is the live one.
        bool Segment(const char* id, const char* label, bool selected, float& x, float centerY,
                     const char* tooltip)
        {
            Theme::ScopedFont font(selected ? Theme::Face::Bold : Theme::Face::UI, Theme::kSizeLabel);

            const ImVec2 min(x, centerY - kItemH * 0.5f);
            const ImVec2 max(x + ImGui::CalcTextSize(label).x + kItemPadX * 2.f, min.y + kItemH);

            const State state = Hit(id, min, max, tooltip);
            // A second track, so the accent slides in when the choice changes rather than
            // appearing on the frame the mouse went down.
            const float chosen = Theme::Fade(state.m_id + 1, selected);

            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImU32 rest = Theme::Blend(IM_COL32(0, 0, 0, 0), Theme::kButtonHov, state.m_lit);
            const ImU32 live = state.m_held ? Theme::kAccentHov : Theme::kAccent;
            draw->AddRectFilled(min, max, Theme::Blend(rest, live, chosen), kInnerRounding);

            const ImU32 text = Theme::Blend(Theme::kTextDim, Theme::kTextItem, state.m_lit);
            CenteredText(draw, min, max, Theme::Blend(text, Theme::kOnAccent, chosen), label);

            x = max.x;
            return state.m_pressed;
        }

        //! The well a segmented group sits in. Drawn before the segments, so its width has
        //! to be known first.
        void SegmentWell(float x, float centerY, float width)
        {
            const ImVec2 min(x - kWellPad, centerY - kItemH * 0.5f - kWellPad);
            const ImVec2 max(x + width + kWellPad, centerY + kItemH * 0.5f + kWellPad);
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, Theme::kButton, kRounding);
        }

        constexpr float kCaretWidth = Theme::Px(7.f);

        //! An outlined button. `glyphWidth` reserves room at its left for the caller to
        //! paint into; `caret` marks it as opening a list.
        bool Chip(const char* id, const char* label, float glyphWidth, bool caret, float& x,
                  float centerY, const char* tooltip, ImVec2* outGlyphCenter = nullptr)
        {
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);

            const float gap   = (glyphWidth > 0.f) ? Theme::Px(6.f) : 0.f;
            const float caretRoom = caret ? kCaretWidth + Theme::Px(7.f) : 0.f;
            const float width = ImGui::CalcTextSize(label).x + glyphWidth + gap + caretRoom
                              + kChipPadX * 2.f;

            const ImVec2 min(x, centerY - kItemH * 0.5f);
            const ImVec2 max(x + width, min.y + kItemH);

            const State state = Hit(id, min, max, tooltip);

            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(min, max,
                                Theme::Blend(Theme::kButton,
                                             state.m_held ? Theme::kButtonHov : Theme::kBlockBg,
                                             state.m_lit),
                                kRounding);
            draw->AddRect(min, max, Theme::Blend(Theme::kDivider, Theme::kBorderHover, state.m_lit),
                          kRounding);

            const float textX = min.x + kChipPadX + glyphWidth + gap;
            draw->AddText(Theme::Snap(ImVec2(textX, (min.y + max.y - ImGui::GetFontSize()) * 0.5f)),
                          Theme::Blend(Theme::kTextItem, Theme::kTextStrong, state.m_lit), label);

            if (caret)
            {
                const float cx = max.x - kChipPadX - kCaretWidth * 0.5f;
                const float cy = (min.y + max.y) * 0.5f;
                draw->AddTriangleFilled(ImVec2(cx - kCaretWidth * 0.5f, cy - kCaretWidth * 0.25f),
                                        ImVec2(cx + kCaretWidth * 0.5f, cy - kCaretWidth * 0.25f),
                                        ImVec2(cx, cy + kCaretWidth * 0.4f), Theme::kTextDimmer);
            }

            if (outGlyphCenter)
            {
                *outGlyphCenter = ImVec2(min.x + kChipPadX + glyphWidth * 0.5f,
                                         (min.y + max.y) * 0.5f);
            }

            x = max.x;
            return state.m_pressed;
        }

        //! A toggle: accent wash and border when on, the plain chip's colours when off.
        bool ToggleChip(const char* id, const char* label, bool on, float& x, float centerY,
                        const char* tooltip)
        {
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);

            const ImVec2 min(x, centerY - kItemH * 0.5f);
            const ImVec2 max(x + ImGui::CalcTextSize(label).x + kItemPadX * 2.f, min.y + kItemH);

            const State state = Hit(id, min, max, tooltip);
            const float lit   = Theme::Fade(state.m_id + 1, on);

            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(min, max,
                                Theme::Blend(Theme::kButton, Theme::kAccentWash, lit), kRounding);
            draw->AddRect(min, max,
                          Theme::Blend(Theme::Blend(Theme::kDivider, Theme::kBorderHover, state.m_lit),
                                       Theme::kAccentEdge, lit),
                          kRounding);
            CenteredText(draw, min, max,
                         Theme::Blend(Theme::Blend(Theme::kTextDim, Theme::kTextItem, state.m_lit),
                                      Theme::kText, lit),
                         label);

            x = max.x;
            return state.m_pressed;
        }

        void Divider(float& x, float centerY)
        {
            x += kGap;
            const float half = Theme::Px(9.f);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, centerY - half), ImVec2(x, centerY + half),
                                                Theme::kDivider);
            x += 1.f + kGap;
        }

        //! Mono text on the bar's baseline, left-aligned at `x`, which it advances.
        void MonoText(float& x, float centerY, ImU32 color, const char* text)
        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
            ImGui::GetWindowDrawList()->AddText(
                Theme::Snap(ImVec2(x, centerY - ImGui::GetFontSize() * 0.5f)), color, text);
            x += ImGui::CalcTextSize(text).x;
        }

        //! A read-only value in a small frame -- the camera's speed, and anything like it.
        void ValueBox(float& x, float centerY, const char* text)
        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);

            const ImVec2 padding(Theme::Px(7.f), Theme::Px(3.f));
            const ImVec2 size = ImGui::CalcTextSize(text);
            const ImVec2 min(x, centerY - (size.y + padding.y * 2.f) * 0.5f);
            const ImVec2 max(x + size.x + padding.x * 2.f, min.y + size.y + padding.y * 2.f);

            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(min, max, Theme::kButton, Theme::Px(3.f));
            draw->AddRect(min, max, Theme::kDivider, Theme::Px(3.f));
            draw->AddText(Theme::Snap(ImVec2(min.x + padding.x, min.y + padding.y)),
                          Theme::kTextItem, text);

            x = max.x;
        }

        constexpr int kViewModeCount = 6;

        const char* ViewModeName(int mode)
        {
            constexpr const char* kNames[] = {"Lit",     "Unlit",     "Wireframe",
                                              "Normals", "Occlusion", "Complexity"};
            return kNames[mode];
        }
    }

    float Toolbar::Height()
    {
        return kHeight;
    }

    void Toolbar::Draw()
    {
        ImDrawList*  draw   = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float  width  = ImGui::GetContentRegionAvail().x;

        draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + kHeight), Theme::kBarBg);
        draw->AddLine(ImVec2(origin.x, origin.y + kHeight - 1.f),
                      ImVec2(origin.x + width, origin.y + kHeight - 1.f), Theme::kBorderPanel);

        const float centerY = origin.y + kHeight * 0.5f;
        float       x       = origin.x + kPad;

        {
            struct ToolDef { Tool m_tool; const char* m_label; const char* m_hint; };
            constexpr ToolDef kTools[] = {
                {Tool::Select, "Select", "Pick entities in the viewport"},
                {Tool::Move,   "Move",   "Drag the gizmo's arrows to move"},
                {Tool::Rotate, "Rotate", "Drag the gizmo's rings to rotate"},
                {Tool::Scale,  "Scale",  "Drag the gizmo's handles to scale"}};

            float wellWidth = 0.f;
            for (const ToolDef& tool: kTools)
            {
                wellWidth += LabelWidth(tool.m_label) + kItemPadX * 2.f;
            }
            SegmentWell(x, centerY, wellWidth);

            for (const ToolDef& tool: kTools)
            {
                if (Segment(tool.m_label, tool.m_label, m_tool == tool.m_tool, x, centerY,
                            tool.m_hint))
                {
                    m_tool = tool.m_tool;
                }
            }
        }

        Divider(x, centerY);

        {
            ImVec2 glyph;
            const float triangle = Theme::Px(7.f);
            Chip("##Play", "Play", triangle, false, x, centerY, "Run the scene", &glyph);
            draw->AddTriangleFilled(ImVec2(glyph.x - triangle * 0.4f, glyph.y - triangle * 0.6f),
                                    ImVec2(glyph.x - triangle * 0.4f, glyph.y + triangle * 0.6f),
                                    ImVec2(glyph.x + triangle * 0.6f, glyph.y), Theme::kAccent);
            x += Theme::Px(6.f);

            const float square = Theme::Px(8.f);
            Chip("##Build", "Build", square, false, x, centerY, "Cook assets and build", &glyph);
            draw->AddRect(ImVec2(glyph.x - square * 0.5f, glyph.y - square * 0.5f),
                          ImVec2(glyph.x + square * 0.5f, glyph.y + square * 0.5f),
                          Theme::kTextLabel, 0.f, 0, Theme::Px(1.5f));
        }

        Divider(x, centerY);

        {
            struct SpaceDef { Space m_space; const char* m_label; const char* m_hint; };
            constexpr SpaceDef kSpaces[] = {
                {Space::World, "World", "Transforms follow the world axes"},
                {Space::Local, "Local", "Transforms follow the entity's own axes"}};

            float wellWidth = 0.f;
            for (const SpaceDef& space: kSpaces)
            {
                wellWidth += LabelWidth(space.m_label) + kItemPadX * 2.f;
            }
            SegmentWell(x, centerY, wellWidth);

            for (const SpaceDef& space: kSpaces)
            {
                if (Segment(space.m_label, space.m_label, m_space == space.m_space, x, centerY,
                            space.m_hint))
                {
                    m_space = space.m_space;
                }
            }
        }

        x += kGap;

        {
            const int    current = static_cast<int>(m_viewMode);
            const ImVec2 popupAnchor(x, centerY + kItemH * 0.5f + Theme::Px(3.f));

            if (Chip("##ViewMode", ViewModeName(current), 0.f, true, x, centerY,
                     "What the viewport shades with"))
            {
                ImGui::OpenPopup("##ViewModes");
            }

            ImGui::SetNextWindowPos(popupAnchor);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, Theme::Px(3.f)));
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::kBorderPopup);
            if (ImGui::BeginPopup("##ViewModes"))
            {
                // The font is popped before EndPopup: one still pushed there is an
                // unbalanced stack, and imgui recovers from it instead of drawing.
                {
                    Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);

                    for (int mode = 0; mode < kViewModeCount; ++mode)
                    {
                        const bool selected = mode == current;

                        ImGui::PushID(mode);
                        ImGui::PushStyleColor(ImGuiCol_Header, Theme::kMenuHov);
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::kMenuHov);
                        if (ImGui::Selectable("##Mode", selected, ImGuiSelectableFlags_SpanAvailWidth,
                                              ImVec2(Theme::Px(112.f), Theme::Px(22.f))))
                        {
                            m_viewMode = static_cast<ViewMode>(mode);
                        }
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();

                        const ImVec2 min = ImGui::GetItemRectMin();
                        const ImVec2 max = ImGui::GetItemRectMax();
                        const float  dot = Theme::Px(9.f);

                        ImDrawList* popupDraw = ImGui::GetWindowDrawList();
                        if (selected)
                        {
                            popupDraw->AddCircleFilled(
                                ImVec2(min.x + Theme::Px(9.f), (min.y + max.y) * 0.5f),
                                Theme::Px(2.5f), Theme::kAccent);
                        }
                        popupDraw->AddText(ImVec2(min.x + Theme::Px(9.f) + dot,
                                                  (min.y + max.y - ImGui::GetFontSize()) * 0.5f),
                                           selected ? Theme::kTextStrong : Theme::kTextItem,
                                           ViewModeName(mode));
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        Divider(x, centerY);

        {
            struct OverlayDef { bool Overlays::* m_member; const char* m_label; const char* m_hint; };
            constexpr OverlayDef kOverlays[] = {
                {&Overlays::m_grid,      "Grid",      "Ground grid"},
                {&Overlays::m_collision, "Collision", "Collision shapes"},
                {&Overlays::m_icons,     "Icons",     "Light and camera icons"},
                {&Overlays::m_stats,     "Stats",     "Frame statistics over the viewport"}};

            for (const OverlayDef& overlay: kOverlays)
            {
                bool& value = m_overlays.*overlay.m_member;
                if (ToggleChip(overlay.m_label, overlay.m_label, value, x, centerY, overlay.m_hint))
                {
                    value = !value;
                }
                x += Theme::Px(5.f);
            }
        }

        Divider(x, centerY);

        {
            MonoText(x, centerY, Theme::kTextDim, "Camera");
            x += Theme::Px(10.f);

            char speed[32];
            snprintf(speed, sizeof(speed), "%.1f m/s", m_cameraSpeed);
            ValueBox(x, centerY, speed);
        }

        {
            char status[64];
            snprintf(status, sizeof(status), "DX12 · %.0f FPS", ImGui::GetIO().Framerate);

            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
            const float right = origin.x + width - kPad - ImGui::CalcTextSize(status).x;
            draw->AddText(ImVec2(right, centerY - ImGui::GetFontSize() * 0.5f),
                          Theme::kTextDim, status);
        }

        // The bar is painted, not laid out; this is what tells the window how tall it is.
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(width, kHeight));
    }
}
