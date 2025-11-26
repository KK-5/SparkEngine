#include "ComponentView.h"

#include <imgui.h>
#include <EASTL/array.h>

#include <ECS/WorldContext.h>
#include <Reflection/TypeRegistry.h>
#include <CoreComponents/Tags.h>
#include <Math/Vector2.h>
#include <Math/Vector3.h>
#include <Math/Vector4.h>
#include <Serialization/UIElement.h>
#include "../../Component/Position.h"
#include "../../Component/AllUIElement.h"

namespace Editor
{
    using namespace Spark;

    void ComponentView::DrawElement(MetaAny& data, eastl::string_view name, const MetaCustom& uiElement, float width)
    {
        auto DrawLabel = [](float labelWidth, float inputWidth, const char* label)
        {
            ImGui::SetNextItemWidth(labelWidth);
            ImGui::AlignTextToFramePadding();
            ImGui::Text(label);
            ImGui::SameLine(labelWidth);
            ImGui::SetNextItemWidth(inputWidth);
            eastl::string labelId = eastl::string("##") + eastl::string(label);
            return labelId;
        };

        if (static_cast<EditTextElement*>(uiElement))
        {
            EditTextElement* ui = static_cast<EditTextElement*>(uiElement);
            if (eastl::string* value = data.try_cast<eastl::string>())
            {
                eastl::string buffer;
                buffer.resize(ui->maxLength);
                strcpy(buffer.data(), value->data());
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ImGui::InputText(label.c_str(), buffer.data(), buffer.size()))
                {
                    data = buffer;
                }
            }
            else
            {
                LOG_ERROR("[ComponentView] EditTextElement expect a string value");
            }
        }
        else if (static_cast<ReadonlyTextElement*>(uiElement))
        {
            ReadonlyTextElement* ui = static_cast<ReadonlyTextElement*>(uiElement);
            if (eastl::string* value = data.try_cast<eastl::string>())
            {
                eastl::string buffer;
                buffer.resize(ui->maxLength);
                strcpy(buffer.data(), value->data());
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), ImGuiInputTextFlags_ReadOnly))
                {
                    LOG_INFO("[ComponentView] Editor text test {}", buffer);
                }
            }
            else
            {
                LOG_ERROR("[ComponentView] ReadonlyTextElement expect a string value");
            }
        }
        else if (static_cast<FloatElement*>(uiElement))
        {
            FloatElement* ui = static_cast<FloatElement*>(uiElement);
            if (float* value = data.try_cast<float>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ImGui::DragFloat(label.c_str(), value, ui->speed, ui->min, ui->max, ui->format.c_str()))
                {
                    LOG_INFO("value: {}", *value);
                }
            }
            else
            {
                LOG_ERROR("[ComponentView] FloatElement expect a float value");
            }
        }
        else if (static_cast<FloatSliderElement*>(uiElement))
        {
            FloatSliderElement* ui = static_cast<FloatSliderElement*>(uiElement);
            if (float* value = data.try_cast<float>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ImGui::SliderFloat(label.c_str(), value, ui->min, ui->max, ui->format.c_str()))
                {
                    LOG_INFO("value: {}", *value);
                }
            }
            else
            {
                LOG_ERROR("[ComponentView] FloatSliderElement expect a float value");
            }
        }
        else if(static_cast<IntElement*>(uiElement))
        {
            IntElement* ui = static_cast<IntElement*>(uiElement);
            if (int* value = data.try_cast<int>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ImGui::DragInt(label.c_str(), value, ui->speed, ui->min, ui->max))
                {
                    LOG_INFO("value: {}", *value);
                }
            }
            else
            {
                LOG_ERROR("[ComponentView] IntElement expect a int value");
            }
        }
        else if(static_cast<IntSliderElement*>(uiElement))
        {
            IntSliderElement* ui = static_cast<IntSliderElement*>(uiElement);
            if (int* value = data.try_cast<int>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ImGui::SliderInt(label.c_str(), value, ui->min, ui->max))
                {
                    LOG_INFO("value: {}", *value);
                }
            }
            else
            {
                LOG_ERROR("[ComponentView] IntSliderElement expect a int value");
            }
        }
        else if(static_cast<BoolElement*>(uiElement))
        {
            if (bool* value = data.try_cast<bool>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ImGui::Checkbox(label.c_str(), value))
                {
                    LOG_INFO("value: {}", *value);
                }
            }
            else
            {
                LOG_ERROR("[ComponentView] BoolElement expect a bool value");
            }
        }
        else if(static_cast<Vec2Element*>(uiElement))
        {
            Vec2Element* ui = static_cast<Vec2Element*>(uiElement);
            if (Math::Vector2* value = data.try_cast<Math::Vector2>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                float inputValue[2] = {value->x, value->y};
                if (ImGui::DragFloat2(label.c_str(), inputValue, ui->speed, ui->min, ui->max, ui->format.c_str()))
                {
                    LOG_INFO("value: {} {}", value->x, value->y);
                }
            }
            else
            {
                LOG_ERROR("[ComponentView] Vec2Element expect a Vector2 value");
            }
        }
        else if(static_cast<Vec3Element*>(uiElement))
        {
            Vec3Element* ui = static_cast<Vec3Element*>(uiElement);
            if (Math::Vector3* value = data.try_cast<Math::Vector3>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                float inputValue[3] = {value->x, value->y, value->z};
                if (ImGui::DragFloat3(label.c_str(), inputValue, ui->speed, ui->min, ui->max, ui->format.c_str()))
                {
                    LOG_INFO("value: {} {}", value->x, value->y);
                }
            }
            else
            {
                LOG_ERROR("[ComponentView] Vec3Element expect a Vector3 value");
            }
        }
        else if(static_cast<ColorElement*>(uiElement))
        {
            ColorElement* ui = static_cast<ColorElement*>(uiElement);
            if (Math::Vector4* value = data.try_cast<Math::Vector4>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                float inputValue[4] = {value->r, value->g, value->b, value->a};
                if (ImGui::ColorEdit4(label.c_str(), inputValue))
                {
                    LOG_INFO("value: {} {}", value->x, value->y);
                }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Error!");
                LOG_ERROR("[ComponentView] ColorElement expect a Vector4 value");
            }
        }
        else if(static_cast<EnumElement*>(uiElement))
        {
            // cast后类型不会被检测成enum，而是检测成int，这里要先保存下来，entt bug?
            MetaType enumType = data.type();
            EnumElement* ui = static_cast<EnumElement*>(uiElement);
            // 使用allow_cast检测是否允许转换
            if (data.allow_cast<int>())
            {
                int value = data.cast<int>();
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                eastl::string inputValue;
                inputValue.resize(256);
                size_t offset = 0;
                for (auto enumValue: enumType.data())
                {
                    strcpy(inputValue.data() + offset, enumValue.second.name());
                    offset += strlen(enumValue.second.name()) + 1;
                }

                if (ImGui::Combo(label.c_str(), &value, inputValue.data(), offset))
                {
                    LOG_INFO("value: {}", value);
                }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "EnumElement expect a enum value!");
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

                DrawElement(value, data.name(), uiElem, availableWidth);
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
            /*
            Position p;
            p.x = 1.f, p.y = 1.f, p.z = 1.f;
            reflectContext.Resolve("Position"_hs).func("AddComponent"_hs).invoke(p, AnyCast(context), entity);
            */
           AllUIElement u;
           reflectContext.Resolve("AllUIElement"_hs).func("AddComponent"_hs).invoke(u, AnyCast(context), entity);

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