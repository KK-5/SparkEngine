#include "MenuBar.h"

#include <imgui.h>

#include <Log/SpdLogSystem.h>
#include <ECS/WorldContext.h>
#include <ECS/NameComponent.h>
#include <SceneManager/IScene.h>
#include <Service/Service.h>
#include "../../Private/Components/Position.h"

namespace Editor
{
    using namespace Spark;

    void MenuBar::Draw(WorldContext& context)
    {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene")) {
                    LOG_INFO("Creating new scene...");
                }
                if (ImGui::MenuItem("Open Scene")) {
                    LOG_INFO("Opening scene...");
                }
                if (ImGui::MenuItem("Save Scene")) {
                    LOG_INFO("Scene saved");
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
                    if (auto scene = Service<IScene>::Get())
                    {
                        //scene->AddEntity(entt);
                        scene->SetParent(entt2, entt);
                        scene->SetParent(entt3, entt2);
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