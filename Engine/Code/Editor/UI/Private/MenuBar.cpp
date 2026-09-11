#include "MenuBar.h"

#include <imgui.h>

#include <Log/ILogSystem.h>
#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <CoreComponents/Name.h>
#include <Hierarchy/IHierarchy.h>
#include <Service/Service.h>
#include "../../Component/Position.h"
#include <Scene/SceneSerializer.h>

namespace Editor
{
    using namespace Spark;

    void MenuBar::Draw()
    {
        auto& context = *WorldExecuteContext::Current();

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene")) {
                    LOG_INFO("Creating new scene...");
                }
                if (ImGui::MenuItem("Open Scene")) {
                    LOG_INFO("Opening scene...");
                }
                if (ImGui::MenuItem("Save Scene")) {
                    // Fixed until the path picker is split out of the asset save dialog.
                    constexpr const char* kScenePath = "project://Scenes/Scene.scene";
                    if (Spark::Scene::SaveScene(kScenePath))
                    {
                        LOG_INFO("[MenuBar] Scene saved to {}.", kScenePath);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) {
                    // 退出逻辑
                }
                ImGui::EndMenu();
            }
        
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {}  // 禁用
                ImGui::Separator();
                if (ImGui::MenuItem("Cut", "Ctrl+X")) {}
                if (ImGui::MenuItem("Copy", "Ctrl+C")) {}
                if (ImGui::MenuItem("Paste", "Ctrl+V")) {}
                ImGui::EndMenu();
            }
        
            if (ImGui::BeginMenu("GameObject")) {
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
        
            if (ImGui::BeginMenu("Window")) {
                //ImGui::MenuItem("Demo Window", NULL, &show_demo_window);
                ImGui::EndMenu();
            }
        
            ImGui::EndMenuBar();
        }
    }
}