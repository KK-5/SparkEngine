#include "ComponentView.h"

#include <imgui.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <ECS/ComponentTraits.h>
#include <Reflection/TypeRegistry.h>
#include <CoreComponents/Tags.h>
#include <ECS/ComponentTraits.h>

#include "EditorTheme.h"
#include "FieldWidgets.h"

namespace Editor
{
    using namespace Spark;

    void ComponentView::DrawComponent(const Spark::MetaType component, Spark::MetaAny& instance)
    {
        ComponentState& state = m_componentState.at(component.id());

        float childWidth = ImGui::GetContentRegionAvail().x;

        ImGui::BeginChild(component.name(), ImVec2(childWidth, 0.0f), ImGuiChildFlags_AutoResizeY,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::PushStyleColor(ImGuiCol_Header, Theme::kBlockBg);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::kButtonHov);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::kBlockBg);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextStrong);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        bool expanded = false;
        {
            Theme::ScopedFont font(Theme::Face::Bold, Theme::kSizeBody);
            expanded = ImGui::CollapsingHeader(component.name(), nullptr, ImGuiTreeNodeFlags_DefaultOpen);
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);

        state.isExpanded = expanded;
        if (expanded)
        {
            float availableWidth = childWidth - (25.f);
            DrawFieldWidgets(component, instance, availableWidth,
                             static_cast<uint32_t>(m_activeEntity));
        }

        ImGui::Dummy(ImVec2(0.0f, Theme::Px(3.f)));
        const ImVec2 rule = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(rule, ImVec2(rule.x + childWidth, rule.y), Theme::kBorderInner);
        ImGui::Dummy(ImVec2(childWidth, 1.f));

        ImGui::EndChild();
    }

    void ComponentView::Draw()
    {
        ImGui::Begin("Component View");

        ReflectContext& reflectContext = TypeRegistry::GetContext();
        ASSERT(WorldExecuteContext::Current(), "There is no world context.");
        auto& context = *WorldExecuteContext::Current();
        auto activeView = context.GetView<ActiveTag>();
        if (activeView.size() != 1)
        {
            ImGui::End();
            return;
        }
        m_activeEntity = activeView.front();

        ImVec2 windowSize = ImGui::GetContentRegionAvail();
        float toolHeight = Theme::Px(28.f);

        eastl::vector<MetaType> components = reflectContext.GetAllTypes();
        ImGui::BeginChild("ComponentTools", ImVec2(windowSize.x, toolHeight), false, ImGuiWindowFlags_NoTitleBar);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kButtonHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kButtonHov);
        if (ImGui::Button(" + "))
        {
            ImGui::OpenPopup("ComponentSelect");
        }
        ImGui::PopStyleColor(3);

        if (ImGui::BeginPopupContextItem("ComponentSelect", ImGuiWindowFlags_NoResize))
        {
            for (MetaType& component: components)
            {
                if (HasComponentFlag(component.traits<ComponentFlags>(), ComponentFlags::Editable))
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
    }
}