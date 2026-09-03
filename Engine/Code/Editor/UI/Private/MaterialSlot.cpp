#include "MaterialSlot.h"

#include <imgui.h>

#include <Material/Components.h>
#include <Reflection/TypeRegistry.h>
#include <Resource/Asset.h>
#include <Resource/AssetTypes.h>

#include "FieldWidgets.h"
#include "MaterialUI.h"
#include "UI/Bus/AssetEditBus.h"
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

        //! What a slot row reports back. Two action sources on one row: the icon inside the
        //! box, and a drop on the box itself.
        struct SlotRowResult
        {
            bool                    iconPressed = false;
            const Resource::Asset*  dropped     = nullptr;
        };

        //! One row of the material slot: label, read-only box, and the row's action as an
        //! icon inside the box — the same shape a texture slot's clear icon has. Laid out
        //! through DrawFieldLabel so both rows line up with every other component's fields.
        //!
        //! `acceptDrop` names the asset type the box takes; Unknown means the row takes no
        //! drop at all.
        SlotRowResult DrawSlotRow(const char* label, const eastl::string& text, float width,
                                  const char* iconPath, const char* tooltip, bool enabled,
                                  Resource::AssetType acceptDrop = Resource::AssetType::Unknown)
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

            SlotRowResult result;

            // Before the icon: a drop target belongs to the item submitted just before it,
            // and the icon button is the next one. Being disabled does not stop the box from
            // taking a drop — ItemAdd records the hovered rect regardless.
            if (acceptDrop != Resource::AssetType::Unknown)
            {
                result.dropped = AcceptAssetDrop(acceptDrop);
            }

            eastl::string iconId = "##Icon";
            iconId += label;
            result.iconPressed = DrawBoxIconButton(iconId.c_str(), iconPath, tooltip, enabled);

            if (textHovered)
            {
                ImGui::SetTooltip("%s", text.c_str());
            }
            return result;
        }
    }

    bool DrawMaterialSlot(const MetaType& owner, TypeId fieldId, MetaData& data,
                          MetaAny& instance, float width, uint32_t worldEntity)
    {
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
            return false;
        }

        const uint32_t handleId = static_cast<uint32_t>(handle);
        const bool     valid    = MaterialExists(handleId);

        bool changed = false;

        // Identity only -- no parameters. Editing the material and editing this one object
        // are two separate surfaces on purpose: mixed into one panel, nothing tells the user
        // whether a slider they just moved changed every object sharing this material or
        // only the one in front of them. The material editor window is the first surface,
        // the override row below is the second.
        const SlotRowResult materialRow =
            DrawSlotRow(data.name(), MaterialIdentity(handleId, valid), width,
                        "editor://edit.svg", "Edit this material", valid,
                        Resource::AssetType::Material);

        if (materialRow.iconPressed)
        {
            MaterialEditBus::Broadcast(&MaterialEditEvents::OpenMaterialEditor, handle);
        }

        // Dropping a `.smat` here points the OBJECT at another material. Only identity
        // travels: the write happens once the asset is ready, and the id is resolved into
        // a material entity on the way in -- one entity per asset, so dropping the same
        // asset on a second object shares this one's material rather than copying it.
        if (materialRow.dropped)
        {
            AssetEditBus::Broadcast(
                &AssetEditEvents::OnMaterialDragToComponent,
                static_cast<Entity>(worldEntity), owner.id(), fieldId,
                materialRow.dropped->GetAssetId());
            changed = true;
        }

        const bool has = HasOverride(worldEntity);

        // Second row, same indent as the first: DrawFieldWidgets indents once per field,
        // and this element draws two.
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
        if (DrawSlotRow("Override", has ? "(this object only)" : "(none)", width,
                        has ? "editor://revert.svg" : "editor://override.svg",
                        has ? "Revert to the material" : "Override on this object only",
                        valid).iconPressed)
        {
            if (has)
            {
                RemoveOverride(worldEntity);
            }
            else
            {
                MetaType paramsType = TypeRegistry::GetContext().Resolve<Resource::StandardPBR>();
                MetaAny  paramsPtr  = paramsType.func("GetComponent"_hs).invoke({}, handleId);
                AddOverride(worldEntity, paramsPtr);
            }
            changed = true;
        }

        return changed;
    }
}
