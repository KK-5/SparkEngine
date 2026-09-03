#pragma once

#include <imgui.h>

//! The editor's colour table, in one place.
//!
//! The values are the mockup's own, not sampled from a picture of it. Three greys of border
//! and five of text is not over-specification -- it is what makes a panel read as layered
//! rather than flat, and collapsing them to one of each is exactly what made the first pass
//! look wrong.
//!
//! Applied per-window today (see Scoped) rather than to ImGui's global style, because only
//! the material editor is drawn to this palette so far and a half-themed editor reads worse
//! than an untouched one. Promoting it later is a matter of moving these same values into
//! SparkImGui's style setup -- which is why the table lives here rather than inside the one
//! window that currently uses it.
namespace Editor::Theme
{
    // Surfaces, darkest to lightest.
    inline constexpr ImU32 kFooterBg  = IM_COL32(0x14, 0x16, 0x19, 0xFF);   // also the tab strip
    inline constexpr ImU32 kWindowBg  = IM_COL32(0x16, 0x18, 0x1C, 0xFF);
    inline constexpr ImU32 kBlockBg   = IM_COL32(0x19, 0x1D, 0x21, 0xFF);   // a raised block
    inline constexpr ImU32 kPreviewBg = IM_COL32(0x1A, 0x1D, 0x21, 0xFF);
    inline constexpr ImU32 kTitleBg   = IM_COL32(0x1C, 0x20, 0x25, 0xFF);

    // Borders, by how far apart the two things they separate are.
    inline constexpr ImU32 kBorderWindow = IM_COL32(0x2A, 0x2F, 0x36, 0xFF);
    inline constexpr ImU32 kBorderPanel  = IM_COL32(0x20, 0x23, 0x29, 0xFF);
    inline constexpr ImU32 kBorderInner  = IM_COL32(0x1C, 0x1F, 0x24, 0xFF);

    // Text, brightest to faintest.
    inline constexpr ImU32 kTextStrong = IM_COL32(0xEE, 0xF2, 0xF5, 0xFF);  // headings
    inline constexpr ImU32 kText       = IM_COL32(0xE2, 0xE6, 0xEA, 0xFF);  // values
    inline constexpr ImU32 kTextLabel  = IM_COL32(0xA7, 0xAD, 0xB7, 0xFF);  // field names
    inline constexpr ImU32 kTextDim    = IM_COL32(0x7A, 0x82, 0x8D, 0xFF);
    inline constexpr ImU32 kTextDimmer = IM_COL32(0x6B, 0x72, 0x80, 0xFF);
    inline constexpr ImU32 kTextFaint  = IM_COL32(0x56, 0x5D, 0x66, 0xFF);  // section headers

    inline constexpr ImU32 kFrameBg     = IM_COL32(0x11, 0x13, 0x16, 0xFF);
    inline constexpr ImU32 kFrameBorder = IM_COL32(0x23, 0x27, 0x2D, 0xFF);
    inline constexpr ImU32 kDivider     = IM_COL32(0x24, 0x28, 0x2E, 0xFF);  // short vertical rules

    inline constexpr ImU32 kButton    = IM_COL32(0x1A, 0x1D, 0x21, 0xFF);
    inline constexpr ImU32 kButtonHov = IM_COL32(0x26, 0x2B, 0x31, 0xFF);   // also "selected"
    inline constexpr ImU32 kScrollbar = IM_COL32(0x2A, 0x2D, 0x32, 0xFF);

    //! Teal, not green -- and what sits ON it is dark, not white.
    inline constexpr ImU32 kAccent    = IM_COL32(0x7F, 0xD6, 0xC2, 0xFF);
    inline constexpr ImU32 kAccentHov = IM_COL32(0xA8, 0xE6, 0xD8, 0xFF);
    inline constexpr ImU32 kOnAccent  = IM_COL32(0x0D, 0x0E, 0x10, 0xFF);

    //! "Modified" is a badge, not a word: amber on a 10%-alpha wash of itself.
    inline constexpr ImU32 kDirty   = IM_COL32(0xE0, 0xA3, 0x5E, 0xFF);
    inline constexpr ImU32 kDirtyBg = IM_COL32(0xE0, 0xA3, 0x5E, 0x1A);

    inline constexpr ImU32 kCloseHovBg   = IM_COL32(0x3A, 0x20, 0x20, 0xFF);
    inline constexpr ImU32 kCloseHovText = IM_COL32(0xE0, 0x73, 0x6A, 0xFF);

    //! Every length in this theme is a mockup pixel; these two turn one into a screen pixel.
    //!
    //! kScale is the monitor's -- what the OS asks every application to scale by. A constant
    //! until there is a reason for it not to be; the honest source is the monitor's content
    //! scale.
    //!
    //! kDesignCorrection is the mockup's. Its type runs about a sixth under desktop
    //! convention (11.5px field labels against VS Code's 13px UI), so obeying the monitor
    //! alone lands short. A property of the drawing, not a user preference -- recorded once
    //! here rather than restated in fifty lengths.
    //!
    //! Deliberately NOT ImGui's style.FontScaleDpi: that one is global, so it would also
    //! enlarge the panels still drawing at the old size and there would be no single place
    //! left that says how big this window is. One factor, applied to text and to spacing
    //! alike -- if only one of the two scaled, the layout would come apart.
    inline constexpr float kScale            = 1.25f;
    inline constexpr float kDesignCorrection = 1.16f;

    constexpr float Px(float value)
    {
        return value * kScale * kDesignCorrection;
    }

    // Type sizes, in mockup pixels. Pass them to ScopedFont, which scales them.
    inline constexpr float kSizeTitle  = 12.5f;   // the window's name
    inline constexpr float kSizeBody   = 12.f;    // buttons, running text
    inline constexpr float kSizeLabel  = 11.5f;   // field names, tabs
    inline constexpr float kSizeMono   = 11.f;    // asset names, paths, values
    inline constexpr float kSizeHeader = 10.f;    // SHADING MODEL and friends

    //! Which typeface, by role. An enum rather than an ImFont* so the palette header does
    //! not have to reach into the UI system to be included.
    enum class Face
    {
        UI,
        Bold,
        Mono,
    };

    //! Pushes a face at a size, pops it on scope exit. `size` is unscaled -- give it one of
    //! the kSize* values.
    //!
    //! Wrap draw-list painting in one too, not just widgets: ImDrawList::AddText and
    //! CalcTextSize both read the CURRENT font, so measuring under one and painting under
    //! another is how text ends up not fitting the box it was measured for.
    class ScopedFont final
    {
    public:
        ScopedFont(Face face, float size);
        ~ScopedFont();

        ScopedFont(const ScopedFont&)            = delete;
        ScopedFont& operator=(const ScopedFont&) = delete;
    };

    //! Pushes the palette and the matching rounding / spacing, pops it on scope exit.
    //! Construct it BEFORE ImGui::Begin -- window background, padding and rounding are read
    //! there, not at the first widget.
    class Scoped final
    {
    public:
        Scoped();
        ~Scoped();

        Scoped(const Scoped&)            = delete;
        Scoped& operator=(const Scoped&) = delete;

    private:
        int m_colors = 0;
        int m_vars   = 0;
    };
}
