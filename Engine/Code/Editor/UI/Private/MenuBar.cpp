#include "MenuBar.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>

#include <Log/ILogSystem.h>
#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <CoreComponents/Name.h>
#include <Hierarchy/IHierarchy.h>
#include <Service/Service.h>
#include <Feature/UI/ImGui/SparkImGui.h>
#include <Feature/Window/IWindowSystem.h>
#include "../../Component/Position.h"
#include "../../Scene/SceneCommands.h"
#include "UI/Bus/FileDialogBus.h"

#include "EditorIcons.h"
#include "EditorTheme.h"
#include "WindowButtons.h"
#include "WindowChrome.h"

namespace Editor
{
    using namespace Spark;

    namespace
    {

        constexpr const char* kSceneExtension = ".scene";
        constexpr const char* kSceneDir       = "project://Scenes";
        constexpr const char* kNewSceneName   = "NewScene";

        constexpr float kLogoSize  = Theme::Px(15.f);
        constexpr float kBrandGap  = Theme::Px(7.f);
        constexpr float kBrandPadR = Theme::Px(14.f);

        GLFWwindow* NativeWindow()
        {
            auto* window = Service<Window::IWindowSystem>::Get();
            return window ? static_cast<GLFWwindow*>(window->GetWindowHandle()) : nullptr;
        }

        constexpr float kMenuMinWidth = Theme::Px(208.f);
        // Every height inside a menu is whole: BeginMenu's popup takes no NoScrollbar flag.
        constexpr float kRowHeight    = Theme::Whole(Theme::Px(22.f));
        constexpr float kMenuPadY     = Theme::Whole(Theme::Px(4.f));
        constexpr float kRowPadX      = Theme::Px(12.f);
        constexpr float kRowGap       = Theme::Px(16.f);   // label to accelerator
        constexpr float kIconSize     = Theme::Px(13.f);
        constexpr float kIconGutter   = kIconSize + Theme::Px(8.f);

        //! Whether the open menu keeps an icon column. Per menu, so every label in it lines
        //! up whether or not its own row has an icon.
        bool s_iconGutter = false;

        //! Styles the label in the bar, and the popup it opens. Close with EndTopMenu.
        bool BeginTopMenu(const char* label, bool iconGutter = false)
        {
            s_iconGutter = iconGutter;

            // Read by the popup's own Begin, inside BeginMenu.
            ImGui::SetNextWindowSizeConstraints(ImVec2(kMenuMinWidth, 0.f), ImVec2(FLT_MAX, FLT_MAX));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, kMenuPadY));
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::kBorderPopup);

            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextLabel);
            ImGui::PushFont(Spark::UI::Fonts::UI(), Theme::Px(Theme::kSizeMenu));
            const bool open = ImGui::BeginMenu(label);
            ImGui::PopFont();
            ImGui::PopStyleColor();

            if (open)
            {
                // Inside the popup only: in the bar, ItemSpacing.x is what pads a menu's
                // label and separates it from the next one.
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
            }
            else
            {
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            return open;
        }

        void EndTopMenu()
        {
            ImGui::PopStyleVar();
            ImGui::EndMenu();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        //! One row: icon, label, accelerator right in mono. Painted rather than left to
        //! MenuItem, which draws them all in the one current font. `icon` is drawn only in a
        //! menu opened with an icon gutter.
        bool MenuRow(const char* label, const char* shortcut = nullptr, bool enabled = true,
                     const Icons::Icon* icon = nullptr)
        {
            const float gutter = s_iconGutter ? kIconGutter : 0.f;

            float labelWidth    = 0.f;
            float shortcutWidth = 0.f;
            {
                Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeBody);
                labelWidth = ImGui::CalcTextSize(label).x;
            }
            if (shortcut)
            {
                Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeShortcut);
                shortcutWidth = ImGui::CalcTextSize(shortcut).x + kRowGap;
            }

            // The measured width grows the auto-sized popup; SpanAvailWidth keeps the
            // highlight the popup's full width whatever that comes out as.
            const float width = kRowPadX * 2.f + gutter + labelWidth + shortcutWidth;

            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::kMenuHov);
            ImGui::PushID(label);
            const bool clicked = ImGui::Selectable("##Row", false,
                enabled ? ImGuiSelectableFlags_SpanAvailWidth
                        : ImGuiSelectableFlags_SpanAvailWidth | ImGuiSelectableFlags_Disabled,
                ImVec2(width, kRowHeight));
            ImGui::PopID();
            ImGui::PopStyleColor();

            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const bool   hovered = enabled && ImGui::IsItemHovered();

            ImDrawList* draw = ImGui::GetWindowDrawList();
            if (s_iconGutter && icon)
            {
                const ImTextureID texture = Icons::Get(*icon);
                if (texture != ImTextureID_Invalid)
                {
                    // A step dimmer than the label, so the column reads as a margin.
                    ImU32 tint = Theme::kTextFaint;
                    if (enabled)
                    {
                        tint = hovered ? Theme::kTextStrong : Theme::kTextDim;
                    }
                    const ImVec2 at(min.x + kRowPadX, (min.y + max.y - kIconSize) * 0.5f);
                    draw->AddImage(texture, at, ImVec2(at.x + kIconSize, at.y + kIconSize),
                                   ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), tint);
                }
            }
            {
                Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeBody);
                ImU32 color = Theme::kTextFaint;
                if (enabled)
                {
                    color = hovered ? Theme::kTextStrong : Theme::kTextItem;
                }
                draw->AddText(ImVec2(min.x + kRowPadX + gutter,
                                     (min.y + max.y - ImGui::GetFontSize()) * 0.5f),
                              color, label);
            }
            if (shortcut)
            {
                Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeShortcut);
                const float x = max.x - kRowPadX - ImGui::CalcTextSize(shortcut).x;
                draw->AddText(ImVec2(x, (min.y + max.y - ImGui::GetFontSize()) * 0.5f),
                              Theme::kTextDimmer, shortcut);
            }
            return clicked && enabled;
        }

        void MenuSeparator()
        {
            const float pad = kMenuPadY;
            ImGui::Dummy(ImVec2(0.f, pad));

            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float  width = ImGui::GetWindowWidth();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(ImGui::GetWindowPos().x, p.y), ImVec2(ImGui::GetWindowPos().x + width, p.y),
                Theme::kBorderWindow);

            ImGui::Dummy(ImVec2(0.f, pad + 1.f));
        }
    }

    void MenuBar::Draw(WindowChrome& chrome)
    {
        if (!ImGui::BeginMenuBar())
        {
            return;
        }

        const ImRect bar = ImGui::GetCurrentWindow()->MenuBarRect();

        DrawBrand(bar.Min.y, bar.GetHeight());
        DrawMenus();

        if (chrome.IsInstalled())
        {
            // The host window sits at the viewport origin, so screen coordinates here are the
            // client coordinates the hit test receives.
            WindowChrome::Caption caption;
            caption.m_height   = bar.Max.y;
            caption.m_dragMinX = ImGui::GetCursorScreenPos().x;
            caption.m_dragMaxX = bar.Max.x - WindowButtonsWidth(true);
            chrome.SetCaption(caption);

            DrawWindowButtons(bar.Max.x, bar.Min.y, bar.GetHeight(), true);
        }

        ImGui::EndMenuBar();
    }

    void MenuBar::OpenScene()
    {
        FileDialogRequest request;
        request.m_mode         = FileDialogMode::Open;
        request.m_title        = "Open Scene";
        request.m_subtitle     = "Scene";
        request.m_extension    = kSceneExtension;
        request.m_defaultDir   = kSceneDir;
        request.m_confirmLabel = "Open";
        request.m_onConfirm    = [this](const eastl::string& path)
        {
            // Asked for here, done after the frame: this runs inside the UI pass, which is
            // the render graph with a command list open.
            if (auto* scene = Service<ISceneCommands>::Get())
            {
                scene->Open(path);
            }
            m_scenePath = path;
            return true;
        };

        FileDialogBus::Broadcast(&FileDialogEvents::OpenFileDialog, request);
    }

    void MenuBar::SaveScene(bool askForPath)
    {
        if (!askForPath)
        {
            if (auto* scene = Service<ISceneCommands>::Get())
            {
                scene->Save(m_scenePath);
            }
            return;
        }

        FileDialogRequest request;
        request.m_mode         = FileDialogMode::Save;
        request.m_title        = "Save Scene As";
        request.m_subtitle     = "Scene";
        request.m_extension    = kSceneExtension;
        request.m_defaultDir   = kSceneDir;
        request.m_defaultName  = kNewSceneName;
        request.m_confirmLabel = "Save";
        request.m_onConfirm    = [this](const eastl::string& path)
        {
            if (auto* scene = Service<ISceneCommands>::Get())
            {
                scene->Save(path);
            }
            m_scenePath = path;
            return true;
        };

        FileDialogBus::Broadcast(&FileDialogEvents::OpenFileDialog, request);
    }

    void MenuBar::DrawBrand(float top, float height)
    {
        ImDrawList*  draw   = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        float        x      = origin.x;

        const ImTextureID logo = Icons::Get(Icons::Icon::App);
        if (logo != ImTextureID_Invalid)
        {
            const float y = top + (height - kLogoSize) * 0.5f;
            draw->AddImage(logo, ImVec2(x, y), ImVec2(x + kLogoSize, y + kLogoSize));
        }
        x += kLogoSize + kBrandGap;

        {
            Theme::ScopedFont bold(Theme::Face::Bold, Theme::kSizeBody);
            draw->AddText(ImVec2(x, top + (height - ImGui::GetFontSize()) * 0.5f), Theme::kTextLabel, "Spark");
            x += ImGui::CalcTextSize("Spark").x;
        }
        {
            Theme::ScopedFont regular(Theme::Face::UI, Theme::kSizeBody);
            draw->AddText(ImVec2(x, top + (height - ImGui::GetFontSize()) * 0.5f), Theme::kTextDimmer, "Engine");
            x += ImGui::CalcTextSize("Engine").x;
        }

        ImGui::Dummy(ImVec2(x - origin.x + kBrandPadR, ImGui::GetFrameHeight()));
    }

    void MenuBar::DrawMenus()
    {
        auto& context = *WorldExecuteContext::Current();

        // Routed globally, so the accelerators the rows advertise also work with every menu
        // closed.
        bool newScene  = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal);
        bool openScene = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal);
        bool saveScene = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal);
        bool saveSAs   = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal);

        if (BeginTopMenu("File", true)) {
            constexpr Icons::Icon kNew  = Icons::Icon::NewScene;
            constexpr Icons::Icon kOpen = Icons::Icon::OpenScene;
            constexpr Icons::Icon kSave = Icons::Icon::Save;
            constexpr Icons::Icon kExit = Icons::Icon::Exit;

            newScene  |= MenuRow("New Scene", "Ctrl+N", true, &kNew);
            openScene |= MenuRow("Open...", "Ctrl+O", true, &kOpen);
            saveScene |= MenuRow("Save", "Ctrl+S", true, &kSave);
            saveSAs   |= MenuRow("Save As...", "Ctrl+Shift+S");
            MenuSeparator();
            if (MenuRow("Exit", "Alt+F4", true, &kExit)) {
                if (GLFWwindow* window = NativeWindow()) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
            }
            EndTopMenu();
        }

        if (newScene) {
            if (auto* scene = Service<ISceneCommands>::Get()) {
                scene->New();
            }
            // The new scene has no file, so its first Save asks for one.
            m_scenePath.clear();
        }
        if (openScene) { OpenScene(); }
        if (saveScene) { SaveScene(m_scenePath.empty()); }
        if (saveSAs)   { SaveScene(true); }

        if (BeginTopMenu("Edit")) {
            MenuRow("Undo", "Ctrl+Z");
            MenuRow("Redo", "Ctrl+Y", false);
            MenuSeparator();
            MenuRow("Cut", "Ctrl+X");
            MenuRow("Copy", "Ctrl+C");
            MenuRow("Paste", "Ctrl+V");
            EndTopMenu();
        }

        if (BeginTopMenu("GameObject")) {
            if (MenuRow("Create Empty")) {
                Entity entt = context.CreateEntity("Parent Entity ");
                Entity entt2 = context.CreateEntity("Sub1 Entity ");
                Entity entt3 = context.CreateEntity("Sub2 Entity ");
                //context.Add<Name>(entt);
                if (auto hierarchy = Service<IHierarchy>::Get())
                {
                    //scene->AddEntity(entt);
                    hierarchy->SetParent(entt2, entt);
                    hierarchy->SetParent(entt3, entt2);
                    Position p;
                    p.x = 0.6;
                    p.y = 6.5;
                    p.z = 9.3;
                    context.Add<Position>(entt, p);
                    LOG_INFO("Created new GameObject");
                }
            }
            if (MenuRow("Create Cube")) {
                LOG_INFO("Created Cube");
            }
            EndTopMenu();
        }

        if (BeginTopMenu("Window")) {
            //MenuRow("Demo Window");
            EndTopMenu();
        }
    }
}
