#include "ComponentView.h"

#include <imgui.h>
#include <EASTL/array.h>

#include <ECS/WorldContext.h>
#include <Reflection/TypeRegistry.h>
#include <CoreComponents/Tags.h>
#include "../../Private/Components/Position.h"

namespace Editor
{
    using namespace Spark;
    void ComponentView::Draw(WorldContext& context)
    {
        ReflectContext& reflectContext = TypeRegistry::GetContext();
        auto activeView = context.GetView<ActiveTag>();
        if (activeView.size() != 1)
        {
            return;
        }
        Entity entity = activeView.front();

        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(35, 35, 35, 255));
        ImGui::Begin("Component View");
        ImVec2 windowSize = ImGui::GetContentRegionAvail();
        float toolHeight = 25.f;

        ImGui::BeginChild("ComponentTools", ImVec2(windowSize.x, toolHeight), false, ImGuiWindowFlags_NoTitleBar);
        if (ImGui::Button("AddComponent"))
        {
            Position p;
            p.x = 1.f, p.y = 1.f, p.z = 1.f;
            reflectContext.Resolve("Position"_hs).func("AddComponent"_hs).invoke(p, AnyCast(context), entity);
        }
        
        ImGui::EndChild();

        ImGui::Separator();

        ImGui::BeginChild("Components");

        eastl::vector<MetaType> components = reflectContext.GetAllTypes();
        for (const MetaType& component: components)
        {
            MetaAny instancePtr = component.func("GetComponent"_hs).invoke({}, AnyCast(context), entity);
            if(!(*instancePtr))
            {
                continue;
            }
            ImGui::BeginChild(component.name());
            ImGui::AlignTextToFramePadding();
            ImGui::SameLine();
            ImGui::TextUnformatted(component.name());
            //ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), component.name());
            for (auto field: component.data())
            {
                ImGui::PushItemWidth(-1);
                MetaData data = field.second;
                MetaAny value = data.get(*instancePtr);
                if (float* result = value.try_cast<float>())
                {
                    //ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), data.name());
                    //ImGui::SameLine();
                    //ImGui::Spacing();
                    //ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), eastl::to_string(*result).c_str());
                    ImGui::DragFloat(data.name(), result, 1.f);
                }
                ImGui::PopItemWidth();
            }
            ImGui::EndChild();
        }
        
        

        ImGui::EndChild();

        ImGui::End();
        ImGui::PopStyleColor();
    }
}