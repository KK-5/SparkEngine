#include "Toolbar.h"

#include <cfloat>
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
        constexpr float kGap      = Theme::Px(14.f);   // around a divider
        constexpr float kFieldGap = Theme::Px(13.f);   // between one labelled field and the next
        constexpr float kLabelGap = Theme::Px(7.f);    // between a field's word and its chip
        constexpr float kItemH    = Theme::Px(26.f);
        constexpr float kItemPadX = Theme::Px(9.f);
        constexpr float kChipPadX = Theme::Px(11.f);
        constexpr float kRounding = Theme::Px(4.f);
        constexpr float kWellPad  = Theme::Px(2.f);
        constexpr float kCaretW   = Theme::Px(7.f);

        //! Concentric with the well around it: the outer radius less the padding between.
        constexpr float kInnerRounding = kRounding - kWellPad;

        //! Whole pixels. A list is exactly as tall as its rows, and a fraction left over is
        //! enough for ImGui to decide the content overflows and grow a scrollbar.
        constexpr float Whole(float value)
        {
            return static_cast<float>(static_cast<int>(value));
        }

        // A dropdown row: a mark column, the label, and an optional shortcut at the right.
        constexpr float kRowH     = Whole(Theme::Px(24.f));
        constexpr float kListPadY = Whole(Theme::Px(4.f));
        constexpr float kRowPadX  = Theme::Px(9.f);
        constexpr float kMarkSide = Theme::Px(12.f);
        constexpr float kMarkRoom = Theme::Px(26.f);   // pad + mark + a gap before the label
        constexpr float kRowGap   = Theme::Px(18.f);

        //! Named after a delay, or the bar flashes a box at every cursor that crosses it. The
        //! colours are pushed because a tooltip is a plain window, and the host window has
        //! WindowBg pushed to transparent.
        void Tooltip(const char* text)
        {
            if (!text || !ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                return;
            }
            ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::kTitleBg);
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::kBorderPopup);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextItem);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Theme::Px(9.f), Theme::Px(6.f)));
            ImGui::SetTooltip("%s", text);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
        }

        //! What a control needs to know about the cursor. `m_lit` is the eased hover, and
        //! `m_held` is the frame-accurate press -- a click must not wait for a fade.
        struct State
        {
            bool    m_pressed = false;
            bool    m_held    = false;
            float   m_lit     = 0.f;
            ImGuiID m_id      = 0;
        };

        //! A hit box with no drawing of its own. `id` is unique within the toolbar.
        State Hit(const char* id, const ImVec2& min, const ImVec2& max, const char* tooltip)
        {
            State state;

            ImGui::SetCursorScreenPos(min);
            state.m_pressed = ImGui::InvisibleButton(id, ImVec2(max.x - min.x, max.y - min.y));
            state.m_held    = ImGui::IsItemActive();
            state.m_id      = ImGui::GetItemID();
            state.m_lit     = Theme::Fade(state.m_id, ImGui::IsItemHovered() || state.m_held);

            Tooltip(tooltip);
            return state;
        }

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

        //! The well a segmented group sits in. Drawn before the segments, so its width has to
        //! be known first.
        void SegmentWell(float x, float centerY, float width)
        {
            const ImVec2 min(x - kWellPad, centerY - kItemH * 0.5f - kWellPad);
            const ImVec2 max(x + width + kWellPad, centerY + kItemH * 0.5f + kWellPad);
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, Theme::kButton, kRounding);
        }

        //! The dim word before a chip. Advances `x` past it.
        void FieldLabel(float& x, float centerY, const char* text)
        {
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeMono);
            ImGui::GetWindowDrawList()->AddText(
                Theme::Snap(ImVec2(x, centerY - ImGui::GetFontSize() * 0.5f)),
                Theme::kTextDimmer, text);
            x += ImGui::CalcTextSize(text).x + kLabelGap;
        }

        //! The widest of a list, so a chip keeps its width when the choice changes and the bar
        //! stops shifting under the cursor.
        float WidestLabel(const char* const* labels, int count)
        {
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);

            float widest = 0.f;
            for (int i = 0; i < count; ++i)
            {
                widest = ImMax(widest, ImGui::CalcTextSize(labels[i]).x);
            }
            return widest;
        }

        //! An outlined button. `glyphWidth` reserves room at its left for the caller to paint
        //! into, `caret` marks it as opening a list, and `minTextWidth` pads it out.
        bool Chip(const char* id, const char* label, float glyphWidth, bool caret, float& x,
                  float centerY, const char* tooltip, ImVec2* outGlyphCenter = nullptr,
                  float minTextWidth = 0.f)
        {
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);

            const float gap       = (glyphWidth > 0.f) ? Theme::Px(6.f) : 0.f;
            const float caretRoom = caret ? kCaretW + Theme::Px(7.f) : 0.f;
            const float textWidth = ImMax(ImGui::CalcTextSize(label).x, minTextWidth);
            const float width     = textWidth + glyphWidth + gap + caretRoom + kChipPadX * 2.f;

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
                const float cx = max.x - kChipPadX - kCaretW * 0.5f;
                const float cy = (min.y + max.y) * 0.5f;
                draw->AddTriangleFilled(
                    ImVec2(cx - kCaretW * 0.5f, cy - kCaretW * 0.25f),
                    ImVec2(cx + kCaretW * 0.5f, cy - kCaretW * 0.25f), ImVec2(cx, cy + kCaretW * 0.4f),
                    Theme::Blend(Theme::kTextDimmer, Theme::kTextItem, state.m_lit));
            }

            if (outGlyphCenter)
            {
                *outGlyphCenter = ImVec2(min.x + kChipPadX + glyphWidth * 0.5f,
                                         (min.y + max.y) * 0.5f);
            }

            x = max.x;
            return state.m_pressed;
        }

        void Divider(float& x, float centerY)
        {
            const float half = Theme::Px(9.f);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, centerY - half), ImVec2(x, centerY + half),
                                                Theme::kDivider);
            x += 1.f + kGap;
        }

        //! Opens a chip's list, anchored under it. EndList balances only the true case.
        bool BeginList(const char* id, const ImVec2& anchor, float minWidth)
        {
            ImGui::SetNextWindowPos(anchor);
            ImGui::SetNextWindowSizeConstraints(ImVec2(minWidth, 0.f), ImVec2(FLT_MAX, FLT_MAX));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, kListPadY));
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::kBorderPopup);

            if (ImGui::BeginPopup(id, ImGuiWindowFlags_NoScrollbar))
            {
                // Rows touch; the list's edges are its only padding. Pushed inside, so it is
                // the popup's own spacing rather than the bar's.
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
                return true;
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            return false;
        }

        void EndList()
        {
            ImGui::PopStyleVar();
            ImGui::EndPopup();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        //! What the mark column of a row carries: one of a set, or one of several switches.
        enum class Mark
        {
            Dot,
            Check,
        };

        //! One row of a list. A Check row leaves the list open -- these come in groups and are
        //! nearly always toggled more than one at a time.
        bool ListRow(const char* label, bool marked, Mark mark, const char* shortcut,
                     const char* tooltip)
        {
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);

            float shortcutWidth = 0.f;
            if (shortcut)
            {
                Theme::ScopedFont mono(Theme::Face::Mono, Theme::kSizeShortcut);
                shortcutWidth = ImGui::CalcTextSize(shortcut).x + kRowGap;
            }

            ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAvailWidth;
            if (mark == Mark::Check)
            {
                flags |= ImGuiSelectableFlags_NoAutoClosePopups;
            }

            // Only a Dot row fills: a checkbox already says what it is, and four filled rows
            // would read as four selections.
            const bool filled = marked && mark == Mark::Dot;

            ImGui::PushID(label);
            ImGui::PushStyleColor(ImGuiCol_Header, Theme::kButtonHov);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::kMenuHov);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::kMenuHov);
            const bool pressed = ImGui::Selectable(
                "##row", filled, flags,
                ImVec2(kMarkRoom + ImGui::CalcTextSize(label).x + shortcutWidth + kRowPadX, kRowH));
            ImGui::PopStyleColor(3);
            ImGui::PopID();

            Tooltip(tooltip);

            const ImVec2 min  = ImGui::GetItemRectMin();
            const ImVec2 max  = ImGui::GetItemRectMax();
            const float  cy   = (min.y + max.y) * 0.5f;
            ImDrawList*  draw = ImGui::GetWindowDrawList();

            // Dot and box share a centre, so the two kinds of list line up with each other.
            const ImVec2 boxMin(min.x + kRowPadX, cy - kMarkSide * 0.5f);
            const ImVec2 boxMax(boxMin.x + kMarkSide, boxMin.y + kMarkSide);

            if (mark == Mark::Dot)
            {
                if (marked)
                {
                    draw->AddCircleFilled(ImVec2((boxMin.x + boxMax.x) * 0.5f, cy), Theme::Px(2.5f),
                                          Theme::kAccent);
                }
            }
            else
            {
                draw->AddRectFilled(boxMin, boxMax, marked ? Theme::kAccent : IM_COL32(0, 0, 0, 0),
                                    Theme::Px(2.f));
                draw->AddRect(boxMin, boxMax, marked ? Theme::kAccent : Theme::kBorderHover,
                              Theme::Px(2.f));
                if (marked)
                {
                    const ImVec2 a(boxMin.x + kMarkSide * 0.24f, cy + kMarkSide * 0.02f);
                    const ImVec2 b(boxMin.x + kMarkSide * 0.42f, boxMin.y + kMarkSide * 0.72f);
                    const ImVec2 c(boxMin.x + kMarkSide * 0.78f, boxMin.y + kMarkSide * 0.28f);
                    draw->AddLine(a, b, Theme::kOnAccent, Theme::Px(1.4f));
                    draw->AddLine(b, c, Theme::kOnAccent, Theme::Px(1.4f));
                }
            }

            draw->AddText(Theme::Snap(ImVec2(min.x + kMarkRoom, cy - ImGui::GetFontSize() * 0.5f)),
                          marked ? Theme::kTextStrong : Theme::kTextItem, label);

            if (shortcut)
            {
                Theme::ScopedFont mono(Theme::Face::Mono, Theme::kSizeShortcut);
                draw->AddText(Theme::Snap(ImVec2(max.x - kRowPadX - ImGui::CalcTextSize(shortcut).x,
                                                 cy - ImGui::GetFontSize() * 0.5f)),
                              Theme::kTextDimmer, shortcut);
            }

            return pressed;
        }

        constexpr int         kToolCount             = 4;
        constexpr const char* kToolNames[kToolCount] = {"Select", "Move", "Rotate", "Scale"};
        constexpr const char* kToolHints[kToolCount] = {"Pick entities in the viewport",
                                                        "Drag the gizmo's arrows to move",
                                                        "Drag the gizmo's rings to rotate",
                                                        "Drag the gizmo's handles to scale"};

        constexpr int         kSpaceCount              = 2;
        constexpr const char* kSpaceNames[kSpaceCount] = {"World", "Local"};
        constexpr const char* kSpaceHints[kSpaceCount] = {"Transforms follow the world axes",
                                                          "Transforms follow the entity's own axes"};

        constexpr int         kViewModeCount                 = 6;
        constexpr const char* kViewModeNames[kViewModeCount] = {"Lit",     "Unlit",     "Wireframe",
                                                                "Normals", "Occlusion", "Complexity"};

        constexpr int         kOverlayCount                = 4;
        constexpr const char* kOverlayNames[kOverlayCount] = {"Grid", "Collision", "Icons", "Stats"};
        constexpr const char* kOverlayHints[kOverlayCount] = {
            "Ground grid", "Collision shapes", "Light and camera icons",
            "Frame statistics over the viewport"};

        constexpr int         kSpeedCount              = 6;
        constexpr float       kSpeeds[kSpeedCount]     = {1.f, 2.f, 5.f, 10.f, 20.f, 50.f};
        constexpr const char* kSpeedNames[kSpeedCount] = {"1.0 m/s",  "2.0 m/s",  "5.0 m/s",
                                                          "10.0 m/s", "20.0 m/s", "50.0 m/s"};
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

        // The one control that is picked often enough to be worth its width: four segments in
        // a well, always visible, no list to open.
        {
            float wellWidth = 0.f;
            for (const char* name: kToolNames)
            {
                wellWidth += LabelWidth(name) + kItemPadX * 2.f;
            }
            SegmentWell(x, centerY, wellWidth);

            for (int i = 0; i < kToolCount; ++i)
            {
                if (Segment(kToolNames[i], kToolNames[i], static_cast<int>(m_tool) == i, x, centerY,
                            kToolHints[i]))
                {
                    m_tool = static_cast<Tool>(i);
                }
            }
        }

        x += kGap;
        Divider(x, centerY);

        // The two things on this bar that do something rather than choose something.
        {
            ImVec2      glyph;
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

        x += kGap;
        Divider(x, centerY);

        // Every choice on this bar is the same shape: a dim word, then a chip that opens a
        // list. The anchor is taken before the chip advances x past itself.
        const auto field = [&](const char* id, const char* label, const char* value,
                               const char* const* options, int count, const char* tooltip,
                               ImVec2& anchor) -> bool
        {
            FieldLabel(x, centerY, label);
            anchor = ImVec2(x, centerY + kItemH * 0.5f + Theme::Px(3.f));

            const bool opened = Chip(id, value, 0.f, true, x, centerY, tooltip, nullptr,
                                     WidestLabel(options, count));
            x += kFieldGap;
            return opened;
        };

        ImVec2 anchor;

        {
            const int space = static_cast<int>(m_space);
            if (field("##Space", "Space", kSpaceNames[space], kSpaceNames, kSpaceCount,
                      "Which axes the gizmo follows", anchor))
            {
                ImGui::OpenPopup("##SpaceList");
            }
            if (BeginList("##SpaceList", anchor, Theme::Px(150.f)))
            {
                for (int i = 0; i < kSpaceCount; ++i)
                {
                    if (ListRow(kSpaceNames[i], i == space, Mark::Dot, nullptr, kSpaceHints[i]))
                    {
                        m_space = static_cast<Space>(i);
                    }
                }
                EndList();
            }
        }

        {
            const int mode = static_cast<int>(m_viewMode);
            if (field("##View", "View", kViewModeNames[mode], kViewModeNames, kViewModeCount,
                      "What the viewport shades with", anchor))
            {
                ImGui::OpenPopup("##ViewList");
            }
            if (BeginList("##ViewList", anchor, Theme::Px(140.f)))
            {
                for (int i = 0; i < kViewModeCount; ++i)
                {
                    if (ListRow(kViewModeNames[i], i == mode, Mark::Dot, nullptr, nullptr))
                    {
                        m_viewMode = static_cast<ViewMode>(i);
                    }
                }
                EndList();
            }
        }

        {
            bool* const flags[kOverlayCount] = {&m_overlays.m_grid, &m_overlays.m_collision,
                                                &m_overlays.m_icons, &m_overlays.m_stats};

            int on = 0;
            for (bool* flag: flags)
            {
                on += *flag ? 1 : 0;
            }

            // A count, not a list of names: the chip says how many are on, the list says which.
            char summary[16];
            snprintf(summary, sizeof(summary), "%d / %d", on, kOverlayCount);
            const char* widest[] = {"0 / 0"};

            if (field("##Show", "Show", summary, widest, 1, "What the viewport draws on top",
                      anchor))
            {
                ImGui::OpenPopup("##ShowList");
            }
            if (BeginList("##ShowList", anchor, Theme::Px(150.f)))
            {
                for (int i = 0; i < kOverlayCount; ++i)
                {
                    if (ListRow(kOverlayNames[i], *flags[i], Mark::Check, nullptr, kOverlayHints[i]))
                    {
                        *flags[i] = !*flags[i];
                    }
                }
                EndList();
            }
        }

        Divider(x, centerY);

        {
            // The nearest step at or below the live speed, so a value set elsewhere still
            // lands on a row rather than showing nothing as current.
            int speed = 0;
            for (int i = 0; i < kSpeedCount; ++i)
            {
                if (kSpeeds[i] <= m_cameraSpeed)
                {
                    speed = i;
                }
            }

            if (field("##Speed", "Camera", kSpeedNames[speed], kSpeedNames, kSpeedCount,
                      "How fast the viewport camera flies", anchor))
            {
                ImGui::OpenPopup("##SpeedList");
            }
            if (BeginList("##SpeedList", anchor, Theme::Px(132.f)))
            {
                for (int i = 0; i < kSpeedCount; ++i)
                {
                    if (ListRow(kSpeedNames[i], i == speed, Mark::Dot, nullptr, nullptr))
                    {
                        m_cameraSpeed = kSpeeds[i];
                    }
                }
                EndList();
            }
        }

        {
            char status[64];
            snprintf(status, sizeof(status), "DX12 · %.0f FPS", ImGui::GetIO().Framerate);

            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
            const float right = origin.x + width - kPad - ImGui::CalcTextSize(status).x;
            draw->AddText(ImVec2(right, centerY - ImGui::GetFontSize() * 0.5f), Theme::kTextDim,
                          status);
        }

        // The bar is painted, not laid out; this is what tells the window how tall it is.
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(width, kHeight));
    }
}
