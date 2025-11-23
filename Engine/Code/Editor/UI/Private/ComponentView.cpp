#include "ComponentView.h"

#include <imgui.h>

#include <ECS/WorldContext.h>

namespace Editor
{
    using namespace Spark;
    void ComponentView::Draw(WorldContext& context)
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(35, 35, 35, 255));
        ImGui::Begin("Component View");
        ImVec2 windowSize = ImGui::GetContentRegionAvail();
        float toolHeight = 25.f;

        ImGui::BeginChild("ComponentTools", ImVec2(windowSize.x, toolHeight), false, ImGuiWindowFlags_NoTitleBar);
        
        ImGui::EndChild();

        ImGui::Separator();

        ImGui::BeginChild("Components");
        
        

        ImGui::EndChild();

        ImGui::End();
        ImGui::PopStyleColor();
    }
}