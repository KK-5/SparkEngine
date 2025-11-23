#include "SceneView.h"

#include <imgui.h>

namespace Editor
{
    void SceneView::Draw()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        flags |= ImGuiWindowFlags_NoInputs;
        ImGui::Begin("Maybe Tools", nullptr, flags);

        if (ImGui::Button("Test")) {
            
        }

        ImGui::End();
    }
}