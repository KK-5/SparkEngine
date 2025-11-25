#include "ComponentView.h"

#include <imgui.h>
#include <EASTL/array.h>

#include <ECS/WorldContext.h>
#include <Reflection/TypeRegistry.h>
#include <CoreComponents/Tags.h>
#include <Serialization/UIElement.h>
#include "../../Private/Components/Position.h"

namespace Editor
{
    using namespace Spark;

    void ComponentView::DrawElement(Spark::MetaAny& data, const Spark::MetaData& field, const Spark::MetaCustom& uiElement)
    {
        if (static_cast<EditTextElement*>(uiElement))
        {
            if (eastl::string* value = data.try_cast<eastl::string>())
            {
                char buffer[256];
                strncpy(buffer, value->c_str(), sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';
                ImGui::AlignTextToFramePadding();
                ImGui::Text(field.name());
                ImGui::SameLine();
                if (ImGui::InputText(field.name(), buffer, sizeof(buffer)))
                {
                    LOG_INFO("[ComponentView] Editor text test {}", buffer);
                }
            }
            else
            {
                LOG_ERROR("[ComponentView] UI element and value is mismatch, expect a string value");
            }
        }

    }


    void ComponentView::Draw(WorldContext& context)
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(35, 35, 35, 255));
        ImGui::Begin("Component View");

        ReflectContext& reflectContext = TypeRegistry::GetContext();
        auto activeView = context.GetView<ActiveTag>();
        if (activeView.size() != 1)
        {
            ImGui::End();
            ImGui::PopStyleColor();
            return;
        }
        Entity entity = activeView.front();

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
            for (auto field: component.data())
            {
                ImGui::PushItemWidth(-1);
                MetaData data = field.second;
                MetaAny value = data.get(*instancePtr);
                MetaCustom uiElem = data.custom();

                if (float* result = value.try_cast<float>())
                {
                    //float value = *result;
                    if (ImGui::DragFloat(data.name(), result, 1.f))
                    {
                        //*result = value;
                        LOG_INFO("value: {}", *result);
                    }
                }
                else
                {
                    DrawElement(value, data, uiElem);
                }

                ImGui::PopItemWidth();
            }
            ImGui::EndChild();
            ImGui::Separator();
        }
        
        

        ImGui::EndChild();

        ImGui::End();
        ImGui::PopStyleColor();
    }
}