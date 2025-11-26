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

    void ComponentView::DrawElement(Spark::MetaAny& data, const Spark::MetaData& field, const Spark::MetaCustom& uiElement, float width)
    {
        if (static_cast<EditTextElement*>(uiElement))
        {
            if (eastl::string* value = data.try_cast<eastl::string>())
            {
                char buffer[256];
                strncpy(buffer, value->c_str(), sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                ImGui::SetNextItemWidth(labelWidth);
                ImGui::AlignTextToFramePadding();
                ImGui::Text(field.name());
                ImVec2 textWidth = ImGui::CalcTextSize(field.name());
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(inputWidth);
                eastl::string label = eastl::string("##") + eastl::string(field.name());
                if (ImGui::InputText(label.c_str(), buffer, sizeof(buffer)))
                {
                    LOG_INFO("[ComponentView] Editor text test {}", buffer);
                }
            }
            else
            {
                LOG_ERROR("[ComponentView] UI element and value is mismatch, expect a string value");
            }
        }
        else if (static_cast<FloatElement*>(uiElement))
        {
            FloatElement* ui = static_cast<FloatElement*>(uiElement);
            if (float* value = data.try_cast<float>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                ImGui::SetNextItemWidth(labelWidth);
                ImGui::AlignTextToFramePadding();
                ImGui::Text(field.name());
                ImVec2 textWidth = ImGui::CalcTextSize(field.name());
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(inputWidth);
                eastl::string label = eastl::string("##") + eastl::string(field.name());
                if (ImGui::DragFloat(label.c_str(), value, ui->speed, ui->min, ui->max, ui->format))
                {
                    LOG_INFO("value: {}", *value);
                }
            }
        }

    }
    
    void ComponentView::DrawComponent(const Spark::MetaType component, Spark::MetaAny& instancePtr)
    {
        float rounding = 5.f;
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f, 3.f));

        ComponentState& state = m_componentState.at(component.id());

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5.0f);
        float childWidth = ImGui::GetContentRegionAvail().x - 10.0f;
        float frameHeight = ImGui::GetFrameHeight();
        size_t elemSize = 0;
        for (auto it = component.data().begin(); it != component.data().end(); ++it)
        {
            elemSize++;
        }

        ImGuiStyle& style = ImGui::GetStyle();
        float titleHeight = ImGui::GetTextLineHeight() + rounding * 2 + style.FramePadding.y * 2 + style.ItemSpacing.y; 
        float elementHeight = ImGui::GetTextLineHeight() + style.FramePadding.y * 2 + style.ItemSpacing.y;
        float totalHeight = titleHeight + (elemSize * elementHeight) + style.WindowPadding.y * 2;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
        ImGui::BeginChild(component.name(), ImVec2(childWidth, state.isExpanded ? totalHeight : titleHeight), 0, 
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor();
        
        // 标题栏
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rounding);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
        if (ImGui::CollapsingHeader(component.name(), nullptr, ImGuiTreeNodeFlags_DefaultOpen)) {
            state.isExpanded = true;
            float availableWidth = childWidth - (2 * 20);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            for (auto field: component.data())
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
                MetaData data = field.second;
                MetaAny value = data.get(*instancePtr);
                MetaCustom uiElem = data.custom();

                DrawElement(value, data, uiElem, availableWidth);
            }
            ImGui::PopStyleColor(3);
        }
        else
        {
            state.isExpanded = false;
        }

        ImGui::PopStyleColor(3);

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        
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
        ImGui::Spacing();

        eastl::vector<MetaType> components = reflectContext.GetAllTypes();
        for (const MetaType& component: components)
        {
            MetaAny instancePtr = component.func("GetComponent"_hs).invoke({}, AnyCast(context), entity);
            if(!(*instancePtr))
            {
                continue;
            }
            
            if (!m_componentState.contains(component.id()))
            {
                m_componentState.emplace(component.id(), ComponentState{component.name(), true});
            }
            
            DrawComponent(component, instancePtr);
        }

        ImGui::End();
        ImGui::PopStyleColor();
    }
}