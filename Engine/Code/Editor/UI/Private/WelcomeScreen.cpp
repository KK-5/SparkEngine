#include "WelcomeScreen.h"

#include <cstdio>

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <VFS/FileSystem.h>
#include <Feature/Window/IWindowSystem.h>
#include <Resource/AssetManagerInterface.h>
#include <Feature/UI/ImGui/IconManagerInterface.h>

#include <GLFW/glfw3.h>

#include "EditorTheme.h"
#include "WindowButtons.h"
#include "WindowChrome.h"

namespace Editor
{
    using namespace Spark;

    namespace
    {
        // Mockup pixels; Theme::Px puts them on this screen. Smaller than the mockup's
        // 1440x900 page, which at 125% outgrows a laptop screen; the column keeps its width.
        constexpr float kDesignWidth  = 1200.f;
        constexpr float kDesignHeight = 750.f;

        //! The strip the window is dragged by, and the height of its buttons.
        constexpr float kCaptionHeight = Theme::Px(34.f);

        //! What the window becomes on the way out. Next to the design size rather than in
        //! Editor.cpp: this screen performs the handover, and the two only mean anything
        //! as a pair. Not scaled -- it is a window size, not a mockup length.
        constexpr Spark::Math::Vector2Int kEditorWindowSize{1920, 1080};
        constexpr float kColumnWidth  = Theme::Px(596.f);
        constexpr float kPadLeft     = Theme::Px(52.f);
        constexpr float kPadTop      = Theme::Px(56.f);
        constexpr float kPadBottom   = Theme::Px(34.f);

        constexpr float kLogoSize    = Theme::Px(42.f);
        constexpr float kLogoGap     = Theme::Px(15.f);

        constexpr float kBlockTopPad = Theme::Px(26.f);
        constexpr float kPhaseRowH   = Theme::Px(25.f);
        constexpr float kGridRowH    = Theme::Px(20.f);
        constexpr float kSpinRowH    = Theme::Px(17.f);
        constexpr float kActionRowH  = Theme::Px(32.f);
        constexpr float kGapPhase    = Theme::Px(17.f);
        constexpr float kGapGrid     = Theme::Px(19.f);
        constexpr float kGapSpin     = Theme::Px(22.f);

        constexpr float kDotSize     = Theme::Px(5.f);
        constexpr float kRowGap      = Theme::Px(9.f);

        // Sizes the shared table has no name for.
        constexpr float kSizeWordmark = 31.f;
        constexpr float kSizeProject  = 24.f;
        constexpr float kSizeTally    = 19.f;
        constexpr float kSizeTallyAll = 13.f;
        constexpr float kSizeRecent   = 13.5f;
        constexpr float kSizeMonoTiny = 10.5f;

        constexpr const char* kLogoPath       = "editor://APP-Icon.svg";
        constexpr const char* kBackgroundPath = "editor://Welcome/Background.png";
        constexpr const char* kProjectMount   = "project://";

        //! Hardcoded: nothing in the build produces a version yet.
        constexpr const char* kVersion  = "0.4.2-dev";
        constexpr const char* kBackend  = "dx12";
        constexpr const char* kPlatform = "win64";

        //! Display order and dot colour. Only types the batch actually holds get a row --
        //! a category with a made-up number is what this screen exists not to have.
        struct Category
        {
            Resource::AssetType type;
            const char*         label;
            ImU32               color;
        };

        constexpr Category kCategories[] = {
            {Resource::AssetType::Shader,   "Shaders",   IM_COL32(0x7F, 0xD6, 0xC2, 0xFF)},
            {Resource::AssetType::Image,    "Textures",  IM_COL32(0x8E, 0xA8, 0xF0, 0xFF)},
            {Resource::AssetType::Model,    "Meshes",    IM_COL32(0xA8, 0xE6, 0xD8, 0xFF)},
            {Resource::AssetType::Material, "Materials", IM_COL32(0xC9, 0x8B, 0xD4, 0xFF)},
        };

        ImTextureID Texture(const Resource::AssetId& id)
        {
            auto* icons = Service<UI::IconManagerInterface>::Get();
            if (!icons || !id.IsValid())
            {
                return ImTextureID_Invalid;
            }
            return icons->RequestIconId(id);
        }

        void Text(ImDrawList* draw, const ImVec2& pos, ImU32 color, const char* text)
        {
            draw->AddText(pos, color, text);
        }

        //! Centres one line of the CURRENT font on a row.
        float CenterY(float top, float height)
        {
            return top + (height - ImGui::GetTextLineHeight()) * 0.5f;
        }

        void Dot(ImDrawList* draw, const ImVec2& center, ImU32 color)
        {
            draw->AddCircleFilled(center, kDotSize * 0.5f, color);
        }

        const Resource::AssetLoadProgress::Entry* FindEntry(
            const Resource::AssetLoadProgress& progress, Resource::AssetType type)
        {
            for (const Resource::AssetLoadProgress::Entry& entry : progress.entries)
            {
                if (entry.type == type)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        eastl::string LastSegment(const eastl::string& path)
        {
            const size_t slash = path.find_last_of("/\\");
            if (slash == eastl::string::npos)
            {
                return path;
            }
            return path.substr(slash + 1);
        }
    }

    void WelcomeScreen::Draw()
    {
        Start();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                                         | ImGuiWindowFlags_NoResize
                                         | ImGuiWindowFlags_NoMove
                                         | ImGuiWindowFlags_NoCollapse
                                         | ImGuiWindowFlags_NoScrollbar
                                         | ImGuiWindowFlags_NoScrollWithMouse
                                         | ImGuiWindowFlags_NoDocking;

        Theme::Scoped theme;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::kAppBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);

        ImGui::Begin("Welcome", nullptr, flags);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size   = ImGui::GetContentRegionAvail();
        if (size.x > 0.f && size.y > 0.f)
        {
            // Half at most: a fixed design width would leave a small window no image pane.
            const float columnWidth = (size.x * 0.5f < kColumnWidth) ? size.x * 0.5f
                                                                     : kColumnWidth;

            DrawRightPane(ImVec2(origin.x + columnWidth, origin.y),
                          ImVec2(size.x - columnWidth, size.y));
            DrawLeftColumn(origin, ImVec2(columnWidth, size.y));
            DrawCaption(origin, size.x);
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    void WelcomeScreen::Start()
    {
        if (m_started)
        {
            return;
        }
        m_started = true;

        LoadImages();
        ReadProject();
        StartPreload();
    }

    Spark::Math::Vector2Int WelcomeScreen::WindowSize()
    {
        return {static_cast<int>(Theme::Px(kDesignWidth)),
                static_cast<int>(Theme::Px(kDesignHeight))};
    }

    void WelcomeScreen::Dismiss()
    {
        m_dismissed = true;

        if (auto* window = Service<Window::IWindowSystem>::Get())
        {
            // Before sizing: changing the style re-fits the frame around the current client.
            glfwSetWindowAttrib(static_cast<GLFWwindow*>(window->GetWindowHandle()),
                                GLFW_RESIZABLE, GLFW_TRUE);
            window->SetWindowSize(m_windowChrome.WindowSizeFor(kEditorWindowSize));
        }
    }

    void WelcomeScreen::DrawCaption(const ImVec2& origin, float width)
    {
        const float buttonsWidth = WindowButtonsWidth(false);

        WindowChrome::Caption caption;
        caption.m_height   = origin.y + kCaptionHeight;
        caption.m_dragMinX = origin.x;
        caption.m_dragMaxX = origin.x + width - buttonsWidth;
        m_windowChrome.SetCaption(caption);

        // The buttons sit on the image; a shade keeps their glyphs readable against a bright sky.
        const float right = origin.x + width;
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
            ImVec2(right - buttonsWidth * 2.f, origin.y), ImVec2(right, origin.y + kCaptionHeight),
            IM_COL32(0x0D, 0x0E, 0x10, 0x00), IM_COL32(0x0D, 0x0E, 0x10, 0xB3),
            IM_COL32(0x0D, 0x0E, 0x10, 0xB3), IM_COL32(0x0D, 0x0E, 0x10, 0x00));

        DrawWindowButtons(right, origin.y, kCaptionHeight, false);
    }

    void WelcomeScreen::LoadImages()
    {
        auto* icons = Service<UI::IconManagerInterface>::Get();
        if (!icons)
        {
            LOG_ERROR("[WelcomeScreen] No IconManager; this screen will draw without art.");
            return;
        }

        m_logoId = icons->OpenIcon(kLogoPath);

        const auto* fileSystem = Service<FileSystem>::Get();
        if (fileSystem && fileSystem->Exists(kBackgroundPath))
        {
            m_backgroundId = icons->OpenIcon(kBackgroundPath);
        }
    }

    void WelcomeScreen::ReadProject()
    {
        const auto* fileSystem = Service<FileSystem>::Get();
        if (!fileSystem)
        {
            return;
        }

        // No project concept yet, so the project IS the mount.
        m_projectPath = fileSystem->ToPhysical(kProjectMount);
        m_projectName = LastSegment(m_projectPath);
    }

    void WelcomeScreen::StartPreload()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        if (!assetManager)
        {
            LOG_ERROR("[WelcomeScreen] No AssetManager; nothing to preload.");
            return;
        }

        m_batch = MakeUnique<Resource::AssetLoadBatch>(assetManager->GetRegisteredAssetIds());
        m_batch->RequestAll();
    }

    void WelcomeScreen::DrawLeftColumn(const ImVec2& origin, const ImVec2& size)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);

        draw->AddRectFilled(origin, end, Theme::kWelcomePanel);
        draw->AddLine(ImVec2(end.x, origin.y), end, Theme::kBorderPanel);

        const float left  = origin.x + kPadLeft;
        const float right = end.x - kPadLeft;
        float       y     = origin.y + kPadTop;

        const ImTextureID logo = Texture(m_logoId);
        if (logo != ImTextureID_Invalid)
        {
            draw->AddImage(logo, ImVec2(left, y), ImVec2(left + kLogoSize, y + kLogoSize));
        }

        {
            Theme::ScopedFont font(Theme::Face::Bold, kSizeWordmark);
            const float textY = CenterY(y, kLogoSize);
            const float sparkW = ImGui::CalcTextSize("Spark").x;
            Text(draw, ImVec2(left + kLogoSize + kLogoGap, textY), Theme::kTextStrong, "Spark");
            Text(draw, ImVec2(left + kLogoSize + kLogoGap + sparkW, textY),
                 Theme::kTextLabel, "Engine");
        }
        y += kLogoSize + Theme::Px(9.f);

        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
            const float indent = left + kLogoSize + kLogoGap;
            float       x      = indent;
            const char* parts[] = {kVersion, kBackend, kPlatform};
            for (size_t i = 0; i < IM_ARRAYSIZE(parts); ++i)
            {
                if (i > 0)
                {
                    Text(draw, ImVec2(x, y), Theme::kBorderWindow, "\xc2\xb7");
                    x += ImGui::CalcTextSize("\xc2\xb7").x + Theme::Px(10.f);
                }
                Text(draw, ImVec2(x, y), Theme::kTextFaint, parts[i]);
                x += ImGui::CalcTextSize(parts[i]).x + Theme::Px(10.f);
            }
            y += ImGui::GetTextLineHeight();
        }
        y += Theme::Px(52.f);

        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeHeader);
            Text(draw, ImVec2(left, y), Theme::kTextFaint, "CURRENT PROJECT");
            y += ImGui::GetTextLineHeight() + Theme::Px(14.f);
        }
        {
            Theme::ScopedFont font(Theme::Face::Bold, kSizeProject);
            Text(draw, ImVec2(left, y), Theme::kTextStrong,
                 m_projectName.empty() ? "No project" : m_projectName.c_str());
            y += ImGui::GetTextLineHeight() + Theme::Px(6.f);
        }
        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
            Text(draw, ImVec2(left, y), Theme::kTextDimmer, m_projectPath.c_str());
            y += ImGui::GetTextLineHeight();
        }
        y += Theme::Px(34.f);

        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeHeader);
            Text(draw, ImVec2(left, y), Theme::kTextFaint, "RECENT");
            y += ImGui::GetTextLineHeight() + Theme::Px(11.f);
        }
        {
            Theme::ScopedFont font(Theme::Face::UI, kSizeRecent);
            Text(draw, ImVec2(left, y), Theme::kTextFaint, "No recent projects");
        }

        DrawPreload(ImVec2(left, origin.y + size.y - kPadBottom), right - left);
    }

    void WelcomeScreen::DrawPreload(const ImVec2& origin, float width)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();

        Resource::AssetLoadProgress progress;
        if (m_batch)
        {
            progress = m_batch->GetProgress();
        }

        const Resource::AssetLoadProgress::Entry* rows[IM_ARRAYSIZE(kCategories)] = {};
        int rowCount = 0;
        for (const Category& category : kCategories)
        {
            if (const auto* entry = FindEntry(progress, category.type))
            {
                rows[rowCount++] = entry;
            }
        }

        const int   gridRows = (rowCount + 1) / 2;
        const float height   = kBlockTopPad + kPhaseRowH + kGapPhase
                             + static_cast<float>(gridRows) * kGridRowH + kGapGrid
                             + kSpinRowH + kGapSpin + kActionRowH;

        float y = origin.y - height;
        draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + width, y), Theme::kBorderInner);
        y += kBlockTopPad;

        {
            float x = origin.x + width;
            {
                Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
                char all[32];
                snprintf(all, sizeof(all), "/ %u", progress.total);
                x -= ImGui::CalcTextSize(all).x;
                Text(draw, ImVec2(x, CenterY(y, kPhaseRowH) + Theme::Px(3.f)),
                     Theme::kTextFaint, all);
                x -= Theme::Px(7.f);
            }
            {
                Theme::ScopedFont font(Theme::Face::Mono, kSizeTally);
                char ready[32];
                snprintf(ready, sizeof(ready), "%u", progress.ready);
                x -= ImGui::CalcTextSize(ready).x;
                Text(draw, ImVec2(x, CenterY(y, kPhaseRowH)), Theme::kTextStrong, ready);
            }
            {
                Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeHeader);
                Text(draw, ImVec2(origin.x, CenterY(y, kPhaseRowH) + Theme::Px(3.f)),
                     Theme::kTextFaint,
                     progress.complete ? "PRELOAD COMPLETE" : "PRELOADING ASSETS");
            }
        }
        y += kPhaseRowH + kGapPhase;

        // Column-major, like the mockup.
        {
            const float columnWidth = (width - Theme::Px(26.f)) * 0.5f;
            for (int i = 0; i < rowCount; ++i)
            {
                const Category& category = kCategories[i];
                const Resource::AssetLoadProgress::Entry& entry = *rows[i];

                const int   column = (i >= gridRows) ? 1 : 0;
                const int   row    = (i >= gridRows) ? i - gridRows : i;
                const float x      = origin.x + static_cast<float>(column)
                                   * (columnWidth + Theme::Px(26.f));
                const float top    = y + static_cast<float>(row) * kGridRowH;
                const bool  done   = (entry.ready + entry.failed) >= entry.total;

                Dot(draw, ImVec2(x + kDotSize * 0.5f, top + kGridRowH * 0.5f),
                    done ? category.color : (category.color & 0x00FFFFFF) | 0x59000000);

                {
                    Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeBody);
                    Text(draw, ImVec2(x + kDotSize + kRowGap, CenterY(top, kGridRowH)),
                         Theme::kTextLabel, category.label);
                }
                {
                    Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeLabel);
                    char count[48];
                    snprintf(count, sizeof(count), "%u / %u", entry.ready, entry.total);
                    const float countX = x + columnWidth - ImGui::CalcTextSize(count).x;
                    Text(draw, ImVec2(countX, CenterY(top, kGridRowH)),
                         done ? Theme::kText : Theme::kTextDimmer, count);
                }
            }
        }
        y += static_cast<float>(gridRows) * kGridRowH + kGapGrid;

        {
            Dot(draw, ImVec2(origin.x + kDotSize * 0.5f, y + kSpinRowH * 0.5f),
                progress.complete ? Theme::kAccent : IM_COL32(0x7F, 0xD6, 0xC2, 0x59));

            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
            const char* line = progress.complete ? "Asset registry ready"
                                                 : progress.current.c_str();
            Text(draw, ImVec2(origin.x + kDotSize + kRowGap, CenterY(y, kSpinRowH)),
                 Theme::kTextDimmer, line);
        }
        y += kSpinRowH + kGapSpin;

        DrawActions(ImVec2(origin.x, y), width, progress.complete);
    }

    void WelcomeScreen::DrawActions(const ImVec2& origin, float width, bool complete)
    {
        const float buttonY = origin.y + (kActionRowH - ImGui::GetFrameHeight()) * 0.5f;
        float       x       = origin.x;

        {
            Theme::ScopedFont font(Theme::Face::Bold, Theme::kSizeTitle);
            const char*  label = complete ? "Enter Editor" : "Preparing\xe2\x80\xa6";
            const ImVec2 area(ImGui::CalcTextSize(label).x + Theme::Px(40.f), 0.f);

            ImGui::PushStyleColor(ImGuiCol_Button, complete ? Theme::kAccent : Theme::kButton);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  complete ? Theme::kAccentHov : Theme::kButton);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  complete ? Theme::kAccent : Theme::kButton);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  complete ? Theme::kOnAccent : Theme::kTextFaint);
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  complete ? IM_COL32(0, 0, 0, 0) : Theme::kFrameBorder);

            ImGui::SetCursorScreenPos(ImVec2(x, buttonY));
            ImGui::BeginDisabled(!complete);
            if (ImGui::Button(label, area))
            {
                Dismiss();
            }
            ImGui::EndDisabled();
            x += ImGui::GetItemRectSize().x + Theme::Px(10.f);

            ImGui::PopStyleColor(5);
        }

        {
            // Placeholder: needs a project concept first.
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeTitle);
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::kFrameBorder);

            ImGui::SetCursorScreenPos(ImVec2(x, buttonY));
            ImGui::BeginDisabled(true);
            ImGui::Button("Open Another Project\xe2\x80\xa6");
            ImGui::EndDisabled();

            ImGui::PopStyleColor(5);
        }

        {
            // Not "Reload": RequestAsset returns an already-Ready asset untouched, so this
            // only picks up files added since startup.
            Theme::ScopedFont font(Theme::Face::Mono, kSizeMonoTiny);
            const float labelWidth = ImGui::CalcTextSize("Rescan").x;

            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextFaint);
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));

            ImGui::SetCursorScreenPos(
                ImVec2(origin.x + width - labelWidth - ImGui::GetStyle().FramePadding.x * 2.f,
                       buttonY));
            if (ImGui::Button("Rescan"))
            {
                if (auto* assetManager = Service<Resource::AssetManager>::Get())
                {
                    assetManager->AssetRegistry();
                }
                // Contents are fixed at construction, so a rescan is a new batch.
                StartPreload();
            }

            ImGui::PopStyleColor(5);
        }
    }

    void WelcomeScreen::DrawRightPane(const ImVec2& origin, const ImVec2& size)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);

        const ImTextureID background = Texture(m_backgroundId);
        if (background != ImTextureID_Invalid)
        {
            draw->AddImage(background, origin, end);
        }
        else
        {
            draw->AddRectFilled(origin, end, Theme::kAppBg);
        }

        // Part of the design, not a stand-in for a missing image: they are what keeps the
        // caption readable over a screenshot.
        const float fadeEnd = origin.x + size.x * 0.22f;
        draw->AddRectFilledMultiColor(origin, ImVec2(fadeEnd, end.y),
            Theme::kWelcomePanel, IM_COL32(0x10, 0x12, 0x16, 0x00),
            IM_COL32(0x10, 0x12, 0x16, 0x00), Theme::kWelcomePanel);

        const float shadeTop = end.y - size.y * 0.34f;
        draw->AddRectFilledMultiColor(ImVec2(origin.x, shadeTop), end,
            IM_COL32(0x0D, 0x0E, 0x10, 0x00), IM_COL32(0x0D, 0x0E, 0x10, 0x00),
            IM_COL32(0x0D, 0x0E, 0x10, 0xCC), IM_COL32(0x0D, 0x0E, 0x10, 0xCC));

        if (size.x < Theme::Px(120.f))
        {
            return;
        }

        const char* caption  = "Sponza \xc2\xb7 Global illumination test scene";
        const char* subtitle = "Engine real-time render \xc2\xb7 2026.08";
        float       y        = end.y - Theme::Px(22.f);

        {
            Theme::ScopedFont font(Theme::Face::Mono, kSizeMonoTiny);
            y -= ImGui::GetTextLineHeight();
            Text(draw, ImVec2(end.x - Theme::Px(26.f) - ImGui::CalcTextSize(subtitle).x, y),
                 Theme::kTextDim, subtitle);
            y -= Theme::Px(5.f);
        }
        {
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeTitle);
            y -= ImGui::GetTextLineHeight();
            Text(draw, ImVec2(end.x - Theme::Px(26.f) - ImGui::CalcTextSize(caption).x, y),
                 Theme::kTextLabel, caption);
        }
    }
}
