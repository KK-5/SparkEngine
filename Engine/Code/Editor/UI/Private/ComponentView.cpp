#include "ComponentView.h"

#include <imgui.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <ECS/ComponentTraits.h>
#include <Reflection/TypeRegistry.h>
#include <CoreComponents/Tags.h>
#include <Serialization/MetaTypeTraits.h>

#include "FieldWidgets.h"

namespace Editor
{
    using namespace Spark;

    void ComponentView::DrawComponent(const Spark::MetaType component, Spark::MetaAny& instance)
    {
        float rounding = 5.f;
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f, 3.f));

        ComponentState& state = m_componentState.at(component.id());

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5.0f);
        float childWidth = ImGui::GetContentRegionAvail().x - 10.0f;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
        ImGui::BeginChild(component.name(), ImVec2(childWidth, 0.0f), ImGuiChildFlags_AutoResizeY,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor();
        
        // 标题栏
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rounding);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
        if (ImGui::CollapsingHeader(component.name(), nullptr, ImGuiTreeNodeFlags_DefaultOpen)) {
            state.isExpanded = true;

            float availableWidth = childWidth - (25.f);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);

            DrawFieldWidgets(component, instance, availableWidth,
                             static_cast<uint32_t>(m_activeEntity));
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }
        else
        {
            state.isExpanded = false;
        }

        ImGui::PopStyleColor(3);

        ImGui::Dummy(ImVec2(0.0f, rounding));

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }

    void ComponentView::Draw()
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(35, 35, 35, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
        ImGui::Begin("Component View");

        ReflectContext& reflectContext = TypeRegistry::GetContext();
        ASSERT(WorldExecuteContext::Current(), "There is no world context.");
        auto& context = *WorldExecuteContext::Current();
        auto activeView = context.GetView<ActiveTag>();
        if (activeView.size() != 1)
        {
            ImGui::End();
            ImGui::PopStyleColor(4);
            return;
        }
        m_activeEntity = activeView.front();

        ImVec2 windowSize = ImGui::GetContentRegionAvail();
        float toolHeight = 25.f;

        eastl::vector<MetaType> components = reflectContext.GetAllTypes();
        ImGui::BeginChild("ComponentTools", ImVec2(windowSize.x, toolHeight), false, ImGuiWindowFlags_NoTitleBar);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.18f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.38f, 0.38f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.38f, 0.38f, 0.38f, 1.f));
        if (ImGui::Button(" + "))
        {
            ImGui::OpenPopup("ComponentSelect");
        }
        ImGui::PopStyleColor(3);


        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.38f, 0.38f, 0.38f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.38f, 0.38f, 0.38f, 1.f));
        if (ImGui::BeginPopupContextItem("ComponentSelect", ImGuiWindowFlags_NoResize)) 
        {
            for (MetaType& component: components)
            {
                if (static_cast<uint8_t>(component.traits<MetaTypeTraits>()) & static_cast<uint8_t>(MetaTypeTraits::Editable) ||
                    static_cast<ComponentTraitsRuntime*>(component.custom()) && 
                    static_cast<ComponentTraitsRuntime*>(component.custom())->editable
                )
                {
                    if (ImGui::Selectable(component.name())) {
                        MetaAny instance = component.construct();
                        component.func("AddOrReplaceComponent"_hs).invoke(
                            {}, static_cast<uint32_t>(m_activeEntity), instance);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::Spacing();
                }
            }

            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Spacing();

        for (MetaType& component: components)
        {
            // Only components that sit directly on world entities are listed here.
            // Non-component reflected types have no GetComponent; non-world components
            // (e.g. StandardPBR, reached indirectly and rendered inline via a
            // MaterialRefElement) report IsWorldComponent == false — skip both.
            if (!component.func("GetComponent"_hs))
            {
                continue;
            }
            if (auto worldFn = component.func("IsWorldComponent"_hs);
                worldFn && !worldFn.invoke({}).cast<bool>())
            {
                continue;
            }

            MetaAny instancePtr = component.func("GetComponent"_hs).invoke({}, static_cast<uint32_t>(m_activeEntity));
            if(!(*instancePtr))
            {
                continue;
            }
            
            if (!m_componentState.contains(component.id()))
            {
                m_componentState.emplace(component.id(), ComponentState{component.name(), true});
            }
            MetaAny instance = *instancePtr;
            DrawComponent(component, instance);
        }

        ImGui::End();
        ImGui::PopStyleColor(4);
    }
}