#include "MaterialWindow.h"

#include <cfloat>
#include <cstdio>

#include <imgui.h>

#include <Reflection/TypeRegistry.h>
#include <Resource/Material/MaterialState.h>
#include <Resource/Material/StandardPBR.h>

#include "EditorTheme.h"
#include "FieldWidgets.h"
#include "MaterialUI.h"

namespace Editor
{
    using namespace Spark;

    namespace
    {
        constexpr const char* kWindowId = "MaterialEditor";

        // Mockup pixels, put on this screen. Every length below goes through Theme::Px for
        // the same reason the font sizes do -- half a scaled layout is a broken one.
        constexpr float kTitleHeight  = Theme::Px(34.f);
        constexpr float kFooterHeight = Theme::Px(42.f);

        //! The header band both panels open with — the preview's toolbar row and the
        //! property panel's tab strip. One number for both: the two bands meet at the
        //! divider, so any difference between them reads as a misaligned window.
        //!
        //! The design gives the tab strip 28 and lets the toolbar row come out of its own
        //! padding (6 + 20 + 6 = 32). Four pixels apart is invisible in the mockup, where
        //! the toolbar's rule is #1c1f24 on #16181c; it is not invisible here. Taking 28
        //! for both means the toolbar row (20 tall) sits centred with 4 above and below.
        constexpr float kHeaderHeight = Theme::Px(28.f);
        constexpr float kPanelWidth   = Theme::Px(320.f);   // the property column
        constexpr float kPad          = Theme::Px(12.f);
        constexpr float kBlockPadY    = Theme::Px(11.f);
        constexpr float kCloseSize    = Theme::Px(24.f);

        //! The one shading model there is. A list rather than a string, so the control is
        //! already the shape a second model would need; picking the only entry does nothing.
        //! Editor-side only -- a `.smat` still carries the parameter type's reflected name.
        constexpr const char* kShadingModels[] = {"Standard PBR"};

        constexpr const char* kPreviewShapes[] = {"Sphere", "Cylinder", "Plane"};

        //! Reads a copy of one of the material entity's components, for the rows that only
        //! display one. Goes through the reflected ops like every other ECS access here, so
        //! the window addresses state by (type, entity) and nothing else.
        template <typename T>
        bool ReadComponent(uint32_t handleId, T& out)
        {
            MetaType type = TypeRegistry::GetContext().Resolve<T>();
            if (!type)
            {
                return false;
            }
            auto getFn = type.func("GetComponent"_hs);
            if (!getFn)
            {
                return false;
            }

            MetaAny ptr = getFn.invoke({}, handleId);
            if (!ptr || !(*ptr))
            {
                return false;
            }

            MetaAny instance = *ptr;
            if (const T* value = instance.try_cast<T>())
            {
                out = *value;
                return true;
            }
            return false;
        }

        const char* AlphaModeName(Resource::AlphaMode mode)
        {
            switch (mode)
            {
            case Resource::AlphaMode::Opaque: return "Opaque";
            case Resource::AlphaMode::Mask:   return "Mask";
            case Resource::AlphaMode::Blend:  return "Blend";
            }
            return "-";
        }

        //! A reflected type's fields, read off the material entity. Returns whether any of
        //! them changed this frame.
        //!
        //! The edit entity is the material handle, so an asset dropped on a field resolves
        //! against the material entity -- the path ComponentOperation's binding to the
        //! MaterialContext already provides.
        bool DrawSection(const MetaType& type, uint32_t handleId, float width)
        {
            if (!type)
            {
                return false;
            }
            auto getFn = type.func("GetComponent"_hs);
            if (!getFn)
            {
                return false;
            }

            MetaAny instancePtr = getFn.invoke({}, handleId);
            if (!instancePtr || !(*instancePtr))
            {
                return false;
            }

            MetaAny instance = *instancePtr;
            return DrawFieldWidgets(type, instance, width, handleId);
        }

        void TintedText(ImU32 color, const char* text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
        }

        //! The faint mono header over each block. `right` is placed against `rightEdge`, a
        //! window-local x -- GetContentRegionMax is the panel's edge and knows nothing about
        //! the block's own padding.
        void SectionLabel(const char* label, const char* right, ImU32 rightColor, float rightEdge)
        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeHeader);

            TintedText(Theme::kTextFaint, label);
            if (right)
            {
                ImGui::SameLine(rightEdge - ImGui::CalcTextSize(right).x);
                TintedText(rightColor, right);
            }
        }

        //! A full-width rule between blocks, plus the item that grows the parent by its
        //! one pixel.
        void BlockSeparator(float width)
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + width, p.y), Theme::kBorderInner);
            ImGui::Dummy(ImVec2(width, 1.f));
        }

        //! `height` is the row's, not the pill's own: items on one line are laid out from
        //! its top, so anything shorter than the tallest thing beside it hangs off the top
        //! edge. Giving every item on the row the same height is what centres them.
        bool PillTab(const char* label, bool selected, float height)
        {
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);

            ImGui::PushStyleColor(ImGuiCol_Button, selected ? Theme::kButtonHov : IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, selected ? Theme::kTextStrong : Theme::kTextDim);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Theme::Px(10.f), Theme::Px(3.f)));
            const bool pressed = ImGui::Button(label, ImVec2(0.f, height));
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
            return pressed;
        }

        //! Puts the next text's baseline in the middle of a row of `height` that began at
        //! `top`. AlignTextToFramePadding cannot do it: it offsets by the style's frame
        //! padding, which only centres text in a frame of the style's own height.
        void CenterInRow(float top, float height)
        {
            ImGui::SetCursorPosY(top + (height - ImGui::GetTextLineHeight()) * 0.5f);
        }

        //! A pill of text painted straight onto the draw list -- the "Modified" badge, which
        //! is not an item and must not take a row of its own.
        void Badge(ImVec2 pos, const char* text, ImU32 color, ImU32 background)
        {
            ImDrawList*  draw = ImGui::GetWindowDrawList();
            const ImVec2 size = ImGui::CalcTextSize(text);
            const ImVec2 padding(Theme::Px(6.f), Theme::Px(2.f));

            draw->AddRectFilled(ImVec2(pos.x, pos.y - padding.y),
                                ImVec2(pos.x + size.x + padding.x * 2.f, pos.y + size.y + padding.y),
                                background, Theme::Px(2.f));
            draw->AddText(ImVec2(pos.x + padding.x, pos.y), color, text);
        }
    }

    MaterialWindow::MaterialWindow()
    {
        MaterialEditBus::Handler::BusConnect();
    }

    MaterialWindow::~MaterialWindow()
    {
        if (BusIsConnected())
        {
            BusDisconnect();
        }
    }

    void MaterialWindow::OpenMaterialEditor(Material::MaterialHandle handle)
    {
        m_target    = handle;
        m_open      = true;
        m_focusNext = true;
        m_dirty     = false;
    }

    void MaterialWindow::Draw()
    {
        if (!m_open)
        {
            return;
        }

        const uint32_t      handleId = static_cast<uint32_t>(m_target);
        const bool          exists   = MaterialExists(handleId);
        const eastl::string identity = MaterialIdentity(handleId, exists);

        // Save writes back to the file that backs this material, so it needs one. A
        // sub-asset id names a material inside a model, which is not a file we can write --
        // that material is editable but not saveable, and Save As is its way out.
        Resource::AssetId backing;
        const bool        canSave =
            exists && TryGetMaterialAsset(handleId, backing) && !backing.IsSubAsset();

        // The title bar's dot is the material's own base colour, not a status light.
        ImU32                 swatch = Theme::kTextFaint;
        Resource::StandardPBR params;
        if (ReadComponent(handleId, params))
        {
            swatch = ImGui::ColorConvertFloat4ToU32(
                ImVec4(params.m_baseColor.r, params.m_baseColor.g, params.m_baseColor.b, 1.f));
        }

        // Before Begin: the window's background, padding and rounding are read there.
        Theme::Scoped theme;

        if (m_focusNext)
        {
            ImGui::SetNextWindowFocus();
            m_focusNext = false;
        }
        ImGui::SetNextWindowSize(ImVec2(Theme::Px(908.f), Theme::Px(604.f)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(Theme::Px(560.f), Theme::Px(360.f)),
                                            ImVec2(FLT_MAX, FLT_MAX));

        // No title bar of imgui's: the mockup's is four differently coloured runs in three
        // typefaces plus a colour swatch and a badge, and a native one takes a single string
        // in a single colour and font. Drawing it costs the window's drag, which
        // DrawTitleBar puts back.
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                                     | ImGuiWindowFlags_NoCollapse
                                     | ImGuiWindowFlags_NoScrollbar
                                     | ImGuiWindowFlags_NoScrollWithMouse;

        // End is unconditional: a false Begin means the window is clipped, not that it was
        // never begun.
        if (ImGui::Begin(kWindowId, nullptr, flags))
        {
            Theme::ScopedFont body(Theme::Face::UI, Theme::kSizeBody);

            const float width = ImGui::GetContentRegionAvail().x;

            DrawTitleBar(width, exists ? identity : eastl::string(), swatch);

            const float bodyHeight = ImGui::GetContentRegionAvail().y - kFooterHeight;
            if (bodyHeight > 0.f)
            {
                const ImVec2 origin = ImGui::GetCursorScreenPos();

                if (exists)
                {
                    // The property column keeps its width and the preview absorbs the rest,
                    // rather than both scaling: field rows have a width they stop being
                    // readable below, and a preview does not.
                    const float half   = width * 0.5f;
                    const float rightW = kPanelWidth < half ? kPanelWidth : half;
                    const float leftW  = width - rightW - 1.f;

                    DrawPreviewPanel(handleId, ImVec2(leftW, bodyHeight));

                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(origin.x + leftW + 0.5f, origin.y),
                        ImVec2(origin.x + leftW + 0.5f, origin.y + bodyHeight), Theme::kBorderPanel);

                    ImGui::SetCursorScreenPos(ImVec2(origin.x + leftW + 1.f, origin.y));
                    DrawPropertyPanel(handleId, ImVec2(rightW, bodyHeight));
                }
                else
                {
                    // Reached when the material entity is destroyed while its window is open.
                    const char*  message = "This material no longer exists.";
                    const ImVec2 size    = ImGui::CalcTextSize(message);
                    ImGui::SetCursorScreenPos(ImVec2(origin.x + (width - size.x) * 0.5f,
                                                     origin.y + (bodyHeight - size.y) * 0.5f));
                    TintedText(Theme::kTextDim, message);
                }

                ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + bodyHeight));
            }

            DrawFooter(width, exists ? identity : eastl::string("-"), canSave);
        }
        ImGui::End();
    }

    void MaterialWindow::DrawTitleBar(float width, const eastl::string& name, ImU32 swatch)
    {
        ImDrawList*  draw = ImGui::GetWindowDrawList();
        const ImVec2 p0   = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + width, p0.y + kTitleHeight);

        draw->AddRectFilled(p0, p1, Theme::kTitleBg, ImGui::GetStyle().WindowRounding,
                            ImDrawFlags_RoundCornersTop);
        draw->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y), Theme::kBorderWindow);

        // The bar moves the window, since there is no native one left to grab. AllowOverlap
        // is what lets the close button, submitted after and inside these same pixels, take
        // the hover: without it the bar holds the hovered id and the button is never
        // clickable.
        ImGui::SetCursorScreenPos(p0);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##TitleDrag", ImVec2(width, kTitleHeight));
        if (ImGui::IsItemActive())
        {
            const ImVec2 pos   = ImGui::GetWindowPos();
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(pos.x + delta.x, pos.y + delta.y));
        }

        const float closeMargin = Theme::Px(6.f);
        ImGui::SetCursorScreenPos(
            ImVec2(p1.x - closeMargin - kCloseSize, p0.y + (kTitleHeight - kCloseSize) * 0.5f));
        if (ImGui::InvisibleButton("##Close", ImVec2(kCloseSize, kCloseSize)))
        {
            m_open = false;
        }
        {
            const bool   hovered = ImGui::IsItemHovered();
            const ImVec2 min     = ImGui::GetItemRectMin();
            const ImVec2 max     = ImGui::GetItemRectMax();
            if (hovered)
            {
                draw->AddRectFilled(min, max, Theme::kCloseHovBg, Theme::Px(3.f));
            }

            const ImU32 color = hovered ? Theme::kCloseHovText : Theme::kTextDim;
            const float inset = Theme::Px(7.f);
            draw->AddLine(ImVec2(min.x + inset, min.y + inset),
                          ImVec2(max.x - inset, max.y - inset), color, 1.5f);
            draw->AddLine(ImVec2(max.x - inset, min.y + inset),
                          ImVec2(min.x + inset, max.y - inset), color, 1.5f);
        }

        const float radius = Theme::Px(4.5f);
        const float gap    = Theme::Px(10.f);
        float       x      = p0.x + kPad;

        draw->AddCircleFilled(ImVec2(x + radius, p0.y + kTitleHeight * 0.5f), radius, swatch);
        x += radius * 2.f + gap;

        // Clipped short of the close button: a long material name would otherwise be drawn
        // straight through it.
        draw->PushClipRect(p0, ImVec2(p1.x - closeMargin - kCloseSize - Theme::Px(8.f), p1.y), true);

        // Each run measures and paints under its own face, so the advance is the width that
        // was actually drawn.
        {
            Theme::ScopedFont font(Theme::Face::Bold, Theme::kSizeTitle);
            const char*       title = "Material Editor";
            draw->AddText(ImVec2(x, p0.y + (kTitleHeight - ImGui::GetTextLineHeight()) * 0.5f),
                          Theme::kTextStrong, title);
            x += ImGui::CalcTextSize(title).x + gap;
        }
        if (!name.empty())
        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
            draw->AddText(ImVec2(x, p0.y + (kTitleHeight - ImGui::GetTextLineHeight()) * 0.5f),
                          Theme::kTextDim, name.c_str());
            x += ImGui::CalcTextSize(name.c_str()).x + gap;
        }
        if (m_dirty)
        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeHeader);
            Badge(ImVec2(x, p0.y + (kTitleHeight - ImGui::GetTextLineHeight()) * 0.5f),
                  "Modified", Theme::kDirty, Theme::kDirtyBg);
        }

        draw->PopClipRect();

        ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
    }

    void MaterialWindow::DrawPreviewPanel(uint32_t handleId, const ImVec2& size)
    {
        // AlwaysUseWindowPadding, or there is no horizontal padding at all: imgui drops
        // WindowPadding.x for a child that draws no border, and this theme's children draw
        // none. The vertical half is kept either way, which is what makes the omission read
        // as "everything is flush left" rather than "padding is off".
        // Vertical padding is zero: the header band is kHeaderHeight tall by decree and the
        // row centres itself in it, and the viewport below has to reach the panel's bottom
        // edge. Horizontal padding stays -- it is what keeps the pills off the edge.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Theme::Px(10.f), 0.f));
        ImGui::BeginChild("##Preview", size, ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();

        // Same fill as the property panel's tab strip, so the two bands read as one bar
        // across the window. Equal geometry is not enough on its own: a filled band ends in
        // a dark-to-light step, a bare hairline on the panel colour ends in a soft one, and
        // the eye puts the soft edge lower even when both sit on the same pixel row.
        {
            const ImVec2 bandMin = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                bandMin, ImVec2(bandMin.x + ImGui::GetWindowSize().x, bandMin.y + kHeaderHeight),
                Theme::kFooterBg);
        }

        // One height for everything on the toolbar row, centred in the header band.
        const float rowHeight = ImGui::GetFrameHeight();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (kHeaderHeight - rowHeight) * 0.5f);
        const float rowTop = ImGui::GetCursorPosY();

        for (int i = 0; i < IM_ARRAYSIZE(kPreviewShapes); ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine();
            }
            if (PillTab(kPreviewShapes[i], i == m_previewShape, rowHeight))
            {
                m_previewShape = i;
            }
        }

        // Studio HDRI is a statement about the lighting, not a fourth shape -- hence the
        // rule in front of it and no selected state.
        {
            ImGui::SameLine();
            const ImVec2 p    = ImGui::GetCursorScreenPos();
            const float  rule = Theme::Px(16.f);
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(p.x + Theme::Px(2.f), p.y + (rowHeight - rule) * 0.5f),
                ImVec2(p.x + Theme::Px(2.f), p.y + (rowHeight + rule) * 0.5f), Theme::kDivider);
            ImGui::Dummy(ImVec2(Theme::Px(5.f), rowHeight));

            ImGui::SameLine();
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
            CenterInRow(rowTop, rowHeight);
            TintedText(Theme::kTextDimmer, "Studio HDRI");
        }

        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);

            const char* label = "Live Preview";
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(label).x
                            - Theme::Px(14.f));
            CenterInRow(rowTop, rowHeight);
            TintedText(Theme::kTextDimmer, label);

            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(max.x + Theme::Px(8.f), (min.y + max.y) * 0.5f), Theme::Px(3.f),
                Theme::kAccent);
        }

        // The viewport's place, held but not filled: rendering a material into it is a pass
        // of its own, not a widget. Flat, where the mockup has a radial gradient -- ImDrawList
        // has no radial fill, and faking one with rings costs more than the look is worth.
        // Closed at kHeaderHeight rather than wherever the row's items left the cursor:
        // that position carries ItemSpacing and whatever the tallest item measured, none of
        // which the property panel's strip knows about.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos(
            ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetWindowPos().y + kHeaderHeight));

        const ImVec2 p    = ImGui::GetCursorScreenPos();
        const ImVec2 area = ImGui::GetContentRegionAvail();

        // The item that pairs with the SetCursorScreenPos above. Moving the cursor sets
        // DC.IsSetPos, only submitting an item clears it, and EndChild asserts if it is
        // still set -- everything below here paints through the draw list and would leave
        // it set. It doubles as what tells the child how tall its contents are, which
        // painting alone never does. Same shape as the footer's closing Dummy.
        ImGui::Dummy(area);

        // Spans the panel edge to edge: p.x sits inside the horizontal padding, so both
        // ends give it back. kBorderPanel, not kBorderInner -- it is the same edge the tab
        // strip closes with on the other side of the divider, so it is the same line.
        draw->AddLine(ImVec2(p.x - Theme::Px(10.f), p.y),
                      ImVec2(p.x + area.x + Theme::Px(10.f), p.y), Theme::kBorderPanel);
        draw->AddRectFilled(p, ImVec2(p.x + area.x, p.y + area.y), Theme::kPreviewBg);

        {
            const char*  note = "Preview";
            const ImVec2 s    = ImGui::CalcTextSize(note);
            draw->AddText(ImVec2(p.x + (area.x - s.x) * 0.5f, p.y + (area.y - s.y) * 0.5f),
                          Theme::kTextFaint, note);
        }

        Resource::MaterialState state;
        const bool              hasState = ReadComponent(handleId, state);

        const eastl::string shading = eastl::string("Shading Model  ·  ") + kShadingModels[0];
        const eastl::string blend   = eastl::string("Blend Mode  ·  ")
                                    + (hasState ? AlphaModeName(state.m_alphaMode) : "-");
        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeHeader);

            const float lineHeight = ImGui::GetTextLineHeight();
            const float bottom     = p.y + area.y - Theme::Px(12.f);
            const float left       = p.x + Theme::Px(14.f);
            draw->AddText(ImVec2(left, bottom - lineHeight * 2.f - Theme::Px(4.f)),
                          Theme::kTextDimmer, shading.c_str());
            draw->AddText(ImVec2(left, bottom - lineHeight), Theme::kTextDimmer, blend.c_str());
        }

        ImGui::EndChild();
    }

    void MaterialWindow::DrawPropertyPanel(uint32_t handleId, const ImVec2& size)
    {
        // Two children, not one. The outer holds the tab strip and cannot scroll; the inner
        // holds everything else and does. In one child the strip is content like any other
        // and scrolls away with the fields underneath it, which is not what a tab is.
        ImGui::BeginChild("##PropertyPanel", size, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const float panelWidth = ImGui::GetContentRegionAvail().x;
        float       tabHeight  = 0.f;

        // A one-tab strip. It is the mockup's divider between the panel's header and its
        // contents, and the place a second tab goes when there is one. Its text starts at
        // kPad like every block below, so the panel keeps one left edge all the way down.
        {
            Theme::ScopedFont font(Theme::Face::Bold, Theme::kSizeLabel);

            ImDrawList*  draw = ImGui::GetWindowDrawList();
            const ImVec2 p    = ImGui::GetCursorScreenPos();
            tabHeight         = kHeaderHeight;

            draw->AddRectFilled(p, ImVec2(p.x + panelWidth, p.y + tabHeight), Theme::kFooterBg);
            draw->AddLine(ImVec2(p.x, p.y + tabHeight), ImVec2(p.x + panelWidth, p.y + tabHeight),
                          Theme::kBorderPanel);

            ImGui::SetCursorScreenPos(
                ImVec2(p.x + kPad, p.y + (tabHeight - ImGui::GetTextLineHeight()) * 0.5f));
            TintedText(Theme::kTextStrong, "Properties");

            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            draw->AddLine(ImVec2(min.x - kPad, p.y + tabHeight - 1.f),
                          ImVec2(max.x + kPad, p.y + tabHeight - 1.f), Theme::kAccent, 2.f);

            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + tabHeight));
        }

        ImGui::BeginChild("##PropertyScroll", ImVec2(panelWidth, size.y - tabHeight));

        // The shading model sits on a block of its own, one shade above the panel: it says
        // what the fields below even are, so it is not one of them.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kBlockBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad, kBlockPadY));
        ImGui::BeginChild("##ShadingModelBlock", ImVec2(panelWidth, 0.f),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        {
            Resource::StandardPBR params;
            int                   used = 0;
            if (ReadComponent(handleId, params))
            {
                for (const Resource::AssetId& id: params.m_textures)
                {
                    if (id.IsValid())
                    {
                        ++used;
                    }
                }
            }

            char textures[32];
            snprintf(textures, sizeof(textures), "%d / %d Textures", used,
                     static_cast<int>(Resource::MaterialTexSlotCount));
            SectionLabel("SHADING MODEL", textures, Theme::kAccent,
                         ImGui::GetContentRegionMax().x);

            // Which model this is, as a picker with one entry. It is not editable in any
            // real sense yet: a `.smat` names the shading model by the parameter type's
            // reflected name, and there is one such type.
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);

            int model = 0;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::Combo("##ShadingModel", &model, kShadingModels, IM_ARRAYSIZE(kShadingModels));
        }
        ImGui::EndChild();
        BlockSeparator(panelWidth);

        // Room for the scrollbar and for the indent DrawFieldWidgets puts on every row.
        const float fieldWidth = panelWidth - kPad * 2.f - Theme::Px(24.f);

        const auto block = [&](const char* title, const MetaType& type)
        {
            ImGui::Dummy(ImVec2(0.f, kBlockPadY - ImGui::GetStyle().ItemSpacing.y));
            ImGui::Indent(kPad);
            SectionLabel(title, nullptr, Theme::kTextFaint, 0.f);

            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);
            const bool        changed = DrawSection(type, handleId, fieldWidth);

            ImGui::Unindent(kPad);
            ImGui::Dummy(ImVec2(0.f, Theme::Px(6.f)));
            BlockSeparator(panelWidth);
            return changed;
        };

        ReflectContext& reflect = TypeRegistry::GetContext();
        m_dirty |= block("PROPERTIES", reflect.Resolve<Resource::StandardPBR>());
        m_dirty |= block("STATE", reflect.Resolve<Resource::MaterialState>());

        ImGui::EndChild();   // ##PropertyScroll
        ImGui::EndChild();   // ##PropertyPanel
    }

    void MaterialWindow::DrawFooter(float width, const eastl::string& path, bool canSave)
    {
        ImDrawList*  draw = ImGui::GetWindowDrawList();
        const ImVec2 p0   = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + width, p0.y + kFooterHeight);

        draw->AddRectFilled(p0, p1, Theme::kFooterBg, ImGui::GetStyle().WindowRounding,
                            ImDrawFlags_RoundCornersBottom);
        draw->AddLine(p0, ImVec2(p1.x, p0.y), Theme::kBorderPanel);

        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
            ImGui::SetCursorScreenPos(
                ImVec2(p0.x + kPad, p0.y + (kFooterHeight - ImGui::GetTextLineHeight()) * 0.5f));
            TintedText(Theme::kTextDimmer, path.c_str());
        }

        // Laid out from the right edge inwards, so the one filled button keeps the corner.
        // None of them are wired yet -- all three wait on the write path.
        struct FooterButton
        {
            const char* label;
            bool        accent;
            bool        enabled;
        };
        const FooterButton buttons[] = {
            {"Save", true, canSave}, {"Save As\xE2\x80\xA6", false, true}, {"Revert", false, true}};

        float x = p1.x - kPad;
        for (const FooterButton& button: buttons)
        {
            Theme::ScopedFont font(button.accent ? Theme::Face::Bold : Theme::Face::UI,
                                   Theme::kSizeBody);

            const float extra       = button.accent ? Theme::Px(4.f) : 0.f;
            const float buttonWidth = ImGui::CalcTextSize(button.label).x
                                    + ImGui::GetStyle().FramePadding.x * 2.f + extra;
            x -= buttonWidth;

            ImGui::BeginDisabled(!button.enabled);

            ImGui::SetCursorScreenPos(
                ImVec2(x, p0.y + (kFooterHeight - ImGui::GetFrameHeight()) * 0.5f));
            if (button.accent)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::kAccent);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kAccentHov);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kAccent);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kOnAccent);
                ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
            }
            else
            {
                // Outlined, not filled: the frame border the theme already draws IS the
                // button, so the fill goes away entirely.
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextLabel);
                ImGui::PushStyleColor(ImGuiCol_Border, Theme::kBorderWindow);
            }
            ImGui::Button(button.label, ImVec2(buttonWidth, 0.f));
            ImGui::PopStyleColor(5);
            ImGui::EndDisabled();

            x -= Theme::Px(9.f);
        }

        // The footer is painted, not laid out, so nothing has told the window how tall its
        // contents are -- and a cursor moved past the last item does not say it either.
        // This is the item that does. Without it End() asserts.
        ImGui::SetCursorScreenPos(p0);
        ImGui::Dummy(ImVec2(width, kFooterHeight));
    }
}
