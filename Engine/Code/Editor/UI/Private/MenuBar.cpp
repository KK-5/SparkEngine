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
#include <Feature/UI/ImGui/IconManagerInterface.h>
#include <Feature/UI/ImGui/SparkImGui.h>
#include <Feature/Window/IWindowSystem.h>
#include "../../Component/Position.h"
#include "../../Scene/SceneCommands.h"

#include "EditorTheme.h"
#include "WindowButtons.h"
#include "WindowChrome.h"

namespace Editor
{
    using namespace Spark;

    namespace
    {
        constexpr const char* kLogoPath = "editor://APP-Icon.svg";

        constexpr float kLogoSize  = Theme::Px(15.f);
        constexpr float kBrandGap  = Theme::Px(7.f);
        constexpr float kBrandPadR = Theme::Px(14.f);

        GLFWwindow* NativeWindow()
        {
            auto* window = Service<Window::IWindowSystem>::Get();
            return window ? static_cast<GLFWwindow*>(window->GetWindowHandle()) : nullptr;
        }

        //! Styles only the label in the bar; the items inside keep the default text and size.
        bool BeginTopMenu(const char* label)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextLabel);
            ImGui::PushFont(Spark::UI::Fonts::UI(), Theme::Px(Theme::kSizeMenu));
            const bool open = ImGui::BeginMenu(label);
            ImGui::PopFont();
            ImGui::PopStyleColor();
            return open;
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

    void MenuBar::DrawBrand(float top, float height)
    {
        auto* icons = Service<UI::IconManagerInterface>::Get();
        if (icons && !m_logoId.IsValid())
        {
            m_logoId = icons->OpenIcon(kLogoPath);
        }

        ImDrawList*  draw   = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        float        x      = origin.x;

        if (icons && m_logoId.IsValid())
        {
            const ImTextureID logo = icons->RequestIconId(m_logoId);
            if (logo != ImTextureID_Invalid)
            {
                const float y = top + (height - kLogoSize) * 0.5f;
                draw->AddImage(logo, ImVec2(x, y), ImVec2(x + kLogoSize, y + kLogoSize));
            }
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

        if (BeginTopMenu("File")) {
            // Asked for here, done after the frame: this callback runs inside the UI pass,
            // which is the render graph with a command list open.
            auto* scene = Service<ISceneCommands>::Get();

            // Fixed until the path picker is split out of the asset save dialog.
            constexpr const char* kScenePath = "project://Scenes/Scene.scene";

            if (ImGui::MenuItem("New Scene") && scene) {
                scene->New();
            }
            if (ImGui::MenuItem("Open Scene") && scene) {
                scene->Open(kScenePath);
            }
            if (ImGui::MenuItem("Save Scene") && scene) {
                scene->Save(kScenePath);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                if (GLFWwindow* window = NativeWindow()) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
            }
            ImGui::EndMenu();
        }

        if (BeginTopMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {}  // 禁用
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X")) {}
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {}
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {}
            ImGui::EndMenu();
        }

        if (BeginTopMenu("GameObject")) {
            if (ImGui::MenuItem("Create Empty")) {
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
            if (ImGui::MenuItem("Create Cube")) {
                LOG_INFO("Created Cube");
            }
            ImGui::EndMenu();
        }

        if (BeginTopMenu("Window")) {
            //ImGui::MenuItem("Demo Window", NULL, &show_demo_window);
            ImGui::EndMenu();
        }
    }
}
