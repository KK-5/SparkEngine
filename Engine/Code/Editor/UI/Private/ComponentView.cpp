#include "ComponentView.h"

#include <imgui.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <ECS/ComponentTraits.h>
#include <Reflection/TypeRegistry.h>
#include <CoreComponents/Tags.h>
#include <Material/Components.h>
#include <Resource/AssetJsonSerializer.h>   // AssetIdToDisplayString for the material slot
#include <Serialization/UIElement.h>
#include <Serialization/MetaTypeTraits.h>

#include "FieldWidgets.h"
#include "UI/Bus/MaterialEditBus.h"

namespace Editor
{
    using namespace Spark;

    namespace
    {
        bool HasOverride(uint32_t entityId)
        {
            MetaType type = TypeRegistry::GetContext().Resolve<Material::StandardPBROverride>();
            if (!type)
            {
                return false;
            }
            auto hasFn = type.func("HasComponent"_hs);
            if (!hasFn)
            {
                return false;
            }
            MetaAny r = hasFn.invoke({}, entityId);
            return r && r.cast<bool>();
        }

        //! The only place a StandardPBROverride is created — which is why the type is kept
        //! out of the add-component list.
        //!
        //! `materialParams` is what the object renders with now, and the new override starts
        //! as a copy of it, so adding one changes nothing on screen. All it does is stop the
        //! object following the material it references.
        void AddOverride(uint32_t entityId, const MetaAny& materialParams)
        {
            MetaType type = TypeRegistry::GetContext().Resolve<Material::StandardPBROverride>();
            if (!type)
            {
                return;
            }

            MetaAny seed = type.construct();
            if (!seed)
            {
                return;
            }
            if (materialParams)
            {
                MetaAny current = *materialParams;
                if (const auto* src = current.try_cast<Resource::StandardPBR>())
                {
                    static_cast<Resource::StandardPBR&>(
                        seed.cast<Material::StandardPBROverride&>()) = *src;
                }
            }
            type.func("AddOrReplaceComponent"_hs).invoke({}, entityId, seed);
        }

        void RemoveOverride(uint32_t entityId)
        {
            if (MetaType type = TypeRegistry::GetContext().Resolve<Material::StandardPBROverride>())
            {
                type.func("RemoveComponent"_hs).invoke({}, entityId);
            }
        }

        //! What a material slot shows in place of parameters. Three cases, all of them
        //! something the user needs told apart: the asset it came from, "no asset backs
        //! this one" (the resident default, or one a scene will own outright), and a
        //! reference that resolves to nothing -- a `.smat` that was deleted lands here,
        //! and the object is quietly rendering with the default material.
        eastl::string MaterialIdentity(uint32_t handleId, bool valid)
        {
            if (!valid)
            {
                return "(none)";
            }

            MetaType refType = TypeRegistry::GetContext().Resolve<Material::MaterialAssetRef>();
            if (refType)
            {
                if (auto hasFn = refType.func("HasComponent"_hs))
                {
                    MetaAny has = hasFn.invoke({}, handleId);
                    if (has && has.cast<bool>())
                    {
                        MetaAny refPtr = refType.func("GetComponent"_hs).invoke({}, handleId);
                        if (refPtr)
                        {
                            MetaAny ref = *refPtr;
                            if (const auto* typed = ref.try_cast<Material::MaterialAssetRef>())
                            {
                                return Resource::AssetIdToDisplayString(typed->m_id);
                            }
                        }
                    }
                }
            }
            return "(scene material)";
        }

        //! One row of the material slot: label, read-only box, and the row's action as an
        //! icon inside the box — the same shape a texture slot's clear icon has. Laid out
        //! through DrawFieldLabel so both rows line up with every other component's fields.
        bool DrawSlotRow(const char* label, const eastl::string& text, float width,
                         const char* iconPath, const char* tooltip, bool enabled)
        {
            bool          elided = false;
            eastl::string buffer = BoxText(text, FieldInputWidth(width) - BoxIconSlot(), elided);

            eastl::string id = DrawFieldLabel(width, label);

            ImGui::BeginDisabled(true);
            ImGui::SetNextItemAllowOverlap();
            ImGui::InputText(id.c_str(), buffer.data(), buffer.size(), ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();

            const bool textHovered =
                elided && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);

            eastl::string iconId = "##Icon";
            iconId += label;
            const bool pressed = DrawBoxIconButton(iconId.c_str(), iconPath, tooltip, enabled);

            if (textHovered)
            {
                ImGui::SetTooltip("%s", text.c_str());
            }
            return pressed;
        }
    }

    void ComponentView::DrawElement(const MetaType& component, TypeId fieldId, MetaData& data, MetaAny& instance, float width)
    {
        MetaCustom uiElement = data.custom();

        // Every value-shaped field goes to the shared widgets. What stays here is the one
        // element that is a reference rather than a value -- what to do with a material
        // reference belongs to the panel that owns the object holding it.
        if (!static_cast<MaterialRefElement*>(uiElement))
        {
            DrawFieldWidget(component, fieldId, data, instance, width, m_editEntity);
            return;
        }

        MetaAny fieldValue = data.get(instance);

        // entt may surface an enum-class field as its underlying integer (see the
        // EnumElement note in FieldWidgets.cpp), so accept either the exact handle type
        // or an int.
        Material::MaterialHandle handle = Material::NullMaterial;
        bool gotHandle = false;
        if (Material::MaterialHandle* h = fieldValue.try_cast<Material::MaterialHandle>())
        {
            handle = *h;
            gotHandle = true;
        }
        else if (fieldValue.allow_cast<int>())
        {
            handle = static_cast<Material::MaterialHandle>(static_cast<uint32_t>(fieldValue.cast<int>()));
            gotHandle = true;
        }

        if (!gotHandle)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "MaterialRefElement expect a MaterialHandle value!");
            return;
        }

        const uint32_t  handleId = static_cast<uint32_t>(handle);
        ReflectContext& reflect  = TypeRegistry::GetContext();
        MetaType        paramsType = reflect.Resolve<Resource::StandardPBR>();

        bool valid = false;
        if (paramsType)
        {
            if (auto hasFn = paramsType.func("HasComponent"_hs))
            {
                MetaAny r = hasFn.invoke({}, handleId);
                valid = r && r.cast<bool>();
            }
        }

        // Identity only -- no parameters. Editing the material and editing this one object
        // are two separate surfaces on purpose: mixed into one panel, nothing tells the user
        // whether a slider they just moved changed every object sharing this material or
        // only the one in front of them. The material editor window is the first surface,
        // the override row below is the second.
        if (DrawSlotRow(data.name(), MaterialIdentity(handleId, valid), width,
                        "editor://edit.svg", "Edit this material", valid))
        {
            MaterialEditBus::Broadcast(&MaterialEditEvents::OpenMaterialEditor, handle);
        }

        const uint32_t entityId = static_cast<uint32_t>(m_activeEntity);
        const bool     has      = HasOverride(entityId);

        // Second row, same indent as the first: RenderFields indents once per field, and
        // this element draws two.
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
        if (DrawSlotRow("Override", has ? "(this object only)" : "(none)", width,
                        has ? "editor://revert.svg" : "editor://override.svg",
                        has ? "Revert to the material" : "Override on this object only",
                        valid))
        {
            if (has)
            {
                RemoveOverride(entityId);
            }
            else
            {
                MetaAny paramsPtr = paramsType.func("GetComponent"_hs).invoke({}, handleId);
                AddOverride(entityId, paramsPtr);
            }
        }
    }

    void ComponentView::RenderFields(const MetaType& type, MetaAny& instance, float width)
    {
        for (auto&& [id, data]: type.data())
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            DrawElement(type, id, data, instance, width);
        }
    }

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

            m_editEntity = static_cast<uint32_t>(m_activeEntity);
            RenderFields(component, instance, availableWidth);
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