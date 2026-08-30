#include "ComponentView.h"

#include <imgui.h>
#include <EASTL/array.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <ECS/ComponentTraits.h>
#include <Reflection/TypeRegistry.h>
#include <CoreComponents/Tags.h>
#include <Math/Color.h>
#include <Math/Vector2.h>
#include <Math/Vector3.h>
#include <Math/Vector4.h>
#include <Resource/AssetTypes.h>
#include <Resource/Asset.h>
#include <Resource/AssetJsonSerializer.h>          // AssetIdToDisplayString for the read-only slot
#include <Resource/Image/ImageAsset.h>   // DescriptorForUsage / ImageUsage for texture slots
#include <Material/Components.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaTypeTraits.h>
#include "UI/Bus/AssetEditBus.h"
#include "../../Component/Position.h"
#include "../../Component/AllUIElement.h"

namespace Editor
{
    using namespace Spark;

    namespace
    {
        //! The only way to create a StandardPBROverride — which is why it is kept out of
        //! the add-component list. Returns whether the entity carries one after the click.
        //!
        //! `materialParams` is what the object renders with now; the new override starts
        //! as a copy of it, so adding one changes nothing on screen. It only stops the
        //! object following the material it references.
        bool DrawOverrideToggle(uint32_t entityId, const MetaAny& materialParams)
        {
            MetaType type = TypeRegistry::GetContext().Resolve<Material::StandardPBROverride>();
            if (!type)
            {
                return false;
            }

            bool has = false;
            if (auto hasFn = type.func("HasComponent"_hs))
            {
                MetaAny r = hasFn.invoke({}, entityId);
                has = r && r.cast<bool>();
            }

            if (has)
            {
                if (ImGui::SmallButton("Revert"))
                {
                    type.func("RemoveComponent"_hs).invoke({}, entityId);
                    return false;
                }
                return true;
            }

            if (!ImGui::SmallButton("Override"))
            {
                return false;
            }

            MetaAny seed = type.construct();
            if (!seed)
            {
                return false;
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
            return true;
        }
    }

    void ComponentView::DrawElement(const MetaType& component, TypeId fieldId, MetaData& data, MetaAny& instance, float width)
    {
        eastl::string_view name = data.name();
        MetaCustom uiElement = data.custom();
        MetaAny fieldValue = data.get(instance);

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
            if (eastl::string* value = fieldValue.try_cast<eastl::string>())
            {
                eastl::string buffer;
                buffer.resize(ui->maxLength);
                strcpy(buffer.data(), value->data());
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), ui->readOnly ? ImGuiInputTextFlags_ReadOnly : 0))
                {
                    data.set(instance, eastl::string(buffer));
                    // Sent replace event
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "EditTextElement expect a string value!");
            }
        }
        else if (static_cast<ReadonlyTextElement*>(uiElement))
        {
            ReadonlyTextElement* ui = static_cast<ReadonlyTextElement*>(uiElement);
            if (eastl::string* value = fieldValue.try_cast<eastl::string>())
            {
                eastl::string buffer;
                buffer.resize(ui->maxLength);
                strcpy(buffer.data(), value->data());
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
                ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "ReadonlyTextElement expect a string value!");
            }
        }
        else if (static_cast<FloatElement*>(uiElement))
        {
            FloatElement* ui = static_cast<FloatElement*>(uiElement);
            if (float* value = fieldValue.try_cast<float>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::DragFloat(label.c_str(), value, ui->speed, ui->min, ui->max, ui->format.c_str()))
                {
                    data.set(instance, *value);
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "FloatElement expect a float value!");
            }
        }
        else if (static_cast<FloatSliderElement*>(uiElement))
        {
            FloatSliderElement* ui = static_cast<FloatSliderElement*>(uiElement);
            if (float* value = fieldValue.try_cast<float>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::SliderFloat(label.c_str(), value, ui->min, ui->max, ui->format.c_str()))
                {
                    data.set(instance, *value);
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "FloatElement expect a float value!");
            }
        }
        else if(static_cast<IntElement*>(uiElement))
        {
            IntElement* ui = static_cast<IntElement*>(uiElement);
            if (int* value = fieldValue.try_cast<int>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::DragInt(label.c_str(), value, ui->speed, ui->min, ui->max))
                {
                    data.set(instance, *value);
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "IntElement expect a int value!");
            }
        }
        else if(static_cast<IntSliderElement*>(uiElement))
        {
            IntSliderElement* ui = static_cast<IntSliderElement*>(uiElement);
            if (int* value = fieldValue.try_cast<int>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::SliderInt(label.c_str(), value, ui->min, ui->max))
                {
                    data.set(instance, *value);
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "IntSliderElement expect a int value!");
            }
        }
        else if(static_cast<UIntElement*>(uiElement))
        {
            UIntElement* ui = static_cast<UIntElement*>(uiElement);
            if (uint32_t* value = fieldValue.try_cast<uint32_t>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::DragScalar(label.c_str(), ImGuiDataType_U32, value, ui->speed, &ui->min, &ui->max))
                {
                    data.set(instance, *value);
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "UIntElement expect a uint32_t value!");
            }
        }
        else if(static_cast<UIntSliderElement*>(uiElement))
        {
            UIntSliderElement* ui = static_cast<UIntSliderElement*>(uiElement);
            if (uint32_t* value = fieldValue.try_cast<uint32_t>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::SliderScalar(label.c_str(), ImGuiDataType_U32, value, &ui->min, &ui->max))
                {
                    data.set(instance, *value);
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "UIntSliderElement expect a uint32_t value!");
            }
        }
        else if(static_cast<BoolElement*>(uiElement))
        {
            BoolElement* ui = static_cast<BoolElement*>(uiElement);
            if (bool* value = fieldValue.try_cast<bool>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::Checkbox(label.c_str(), value))
                {
                    data.set(instance, *value);
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "BoolElement expect a bool value!");
            }
        }
        else if(static_cast<Vec2Element*>(uiElement))
        {
            Vec2Element* ui = static_cast<Vec2Element*>(uiElement);
            if (Math::Vector2* value = fieldValue.try_cast<Math::Vector2>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                float inputValue[2] = {value->x, value->y};
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::DragFloat2(label.c_str(), inputValue, ui->speed, ui->min, ui->max, ui->format.c_str()))
                {
                    Math::Vector2 vec2(inputValue[0], inputValue[1]);
                    data.set(instance, vec2);
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Vec2Element expect a Vector2 value!");
            }
        }
        else if(static_cast<Vec3Element*>(uiElement))
        {
            Vec3Element* ui = static_cast<Vec3Element*>(uiElement);
            if (Math::Vector3* value = fieldValue.try_cast<Math::Vector3>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                float inputValue[3] = {value->x, value->y, value->z};
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::DragFloat3(label.c_str(), inputValue, ui->speed, ui->min, ui->max, ui->format.c_str()))
                {
                    Math::Vector3 vec3(inputValue[0], inputValue[1], inputValue[2]);
                    data.set(instance, vec3);
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Vec3Element expect a Vector3 value!");
            }
        }
        else if(static_cast<ColorElement*>(uiElement))
        {
            ColorElement* ui = static_cast<ColorElement*>(uiElement);
            if (Math::Color* value = fieldValue.try_cast<Math::Color>())
            {
                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                float inputValue[4] = {value->r, value->g, value->b, value->a};
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::ColorEdit4(label.c_str(), inputValue))
                {
                    Math::Color edited(inputValue[0], inputValue[1], inputValue[2], inputValue[3]);
                    data.set(instance, edited);
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "ColorElement expect a Color value!");
            }
        }
        else if(static_cast<AssetElement*>(uiElement))
        {
            AssetElement* ui = static_cast<AssetElement*>(uiElement);
            if (Resource::AssetId* value = fieldValue.try_cast<Resource::AssetId>())
            {
                eastl::string display = Resource::AssetIdToDisplayString(*value);
                eastl::string buffer;
                buffer.resize(256);
                strcpy(buffer.data(), display.c_str());

                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), ImGuiInputTextFlags_ReadOnly);
                if (ui->readOnly) { ImGui::EndDisabled(); }

                if (!ui->readOnly && ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DRAG_ASSET_FILE"))
                    {
                        auto* asset = *static_cast<Resource::Asset**>(payload->Data);
                        const bool typeOk = asset
                            && (ui->expectType == 0 || static_cast<uint32_t>(asset->GetAssetType()) == ui->expectType);
                        if (typeOk)
                        {
                            AssetEditBus::Broadcast(
                                &AssetEditEvents::OnAssetDragToComponent,
                                static_cast<Spark::Entity>(m_editEntity), component.id(), fieldId,
                                asset->GetAssetId(), asset->GetAssetType());
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "AssetElement expect an AssetId value!");
            }
        }
        else if(static_cast<TextureElement*>(uiElement))
        {
            TextureElement* ui = static_cast<TextureElement*>(uiElement);
            if (Resource::AssetId* value = fieldValue.try_cast<Resource::AssetId>())
            {
                eastl::string display = Resource::AssetIdToDisplayString(*value);
                eastl::string buffer;
                buffer.resize(256);
                strcpy(buffer.data(), display.c_str());

                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), ImGuiInputTextFlags_ReadOnly);

                // Drop an image here: re-tag its id with this slot's usage descriptor so the
                // texture compiles with the right color space, then request the load. Usage is
                // the asset's own semantic; the slot only picks which usage-variant to bind.
                if (!ui->readOnly && ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DRAG_ASSET_FILE"))
                    {
                        auto* asset = *static_cast<Resource::Asset**>(payload->Data);
                        if (asset && asset->GetAssetType() == Resource::AssetType::Image)
                        {
                            Resource::AssetId usedId = asset->GetAssetId().WithDescriptor(
                                Resource::ImageAsset::DescriptorForUsage(
                                    static_cast<Resource::ImageUsage>(ui->usageHint)));
                            AssetEditBus::Broadcast(
                                &AssetEditEvents::OnAssetDragToComponent,
                                static_cast<Spark::Entity>(m_editEntity), component.id(), fieldId,
                                usedId, asset->GetAssetType());
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Clear the slot back to None — a direct field write (no async load), on the
                // same commit path as the scalar editors (data.set -> ReplaceComponent).
                if (!ui->readOnly && value->IsValid())
                {
                    eastl::string clearId = "Clear##";
                    clearId.append(name.data(), name.size());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(clearId.c_str()))
                    {
                        data.set(instance, Resource::AssetId{});
                    }
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "TextureElement expect an AssetId value!");
            }
        }
        else if(static_cast<EnumElement*>(uiElement))
        {
            // cast后类型不会被检测成enum，而是检测成int，这里要先保存下来，entt bug?
            MetaType enumType = fieldValue.type();
            EnumElement* ui = static_cast<EnumElement*>(uiElement);
            // 使用allow_cast检测是否允许转换
            if (fieldValue.allow_cast<int>())
            {
                int value = fieldValue.cast<int>();
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

                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::Combo(label.c_str(), &value, inputValue.data(), offset))
                {
                    data.set(instance, value);
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "EnumElement expect a enum value!");
            }
        }
        else if(static_cast<MaterialRefElement*>(uiElement))
        {
            // entt may surface an enum-class field as its underlying integer (see the
            // EnumElement note above), so accept either the exact handle type or an int.
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

            if (gotHandle)
            {
                const uint32_t handleId = static_cast<uint32_t>(handle);
                MetaType paramsType = TypeRegistry::GetContext().Resolve<Resource::StandardPBR>();

                bool valid = false;
                if (paramsType)
                {
                    if (auto hasFn = paramsType.func("HasComponent"_hs))
                    {
                        MetaAny r = hasFn.invoke({}, handleId);
                        valid = r && r.cast<bool>();
                    }
                }

                float labelWidth = width * 0.3f;
                float inputWidth = width * 0.7f;
                eastl::string label = DrawLabel(labelWidth, inputWidth, name.data());
                eastl::string buffer;
                buffer.resize(64);
                strcpy(buffer.data(), valid ? "Material" : "None");
                ImGui::BeginDisabled(true);
                ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), ImGuiInputTextFlags_ReadOnly);
                ImGui::EndDisabled();

                MetaAny paramsPtr = valid
                    ? paramsType.func("GetComponent"_hs).invoke({}, handleId)
                    : MetaAny{};

                ImGui::SameLine();
                const bool overridden =
                    DrawOverrideToggle(static_cast<uint32_t>(m_activeEntity), paramsPtr);

                // An override shadows the material completely, so the material's own
                // fields are not what this object renders with — they belong to whoever
                // else references it. The override's component section shows the values
                // that are in effect.
                if (paramsPtr && *paramsPtr && !overridden)
                {
                    MetaAny paramsInstance = *paramsPtr;
                    // Retarget the edit entity so an asset drop inside these inlined
                    // fields resolves against the material handle, not the world entity.
                    const uint32_t prev = m_editEntity;
                    m_editEntity = handleId;
                    RenderFields(paramsType, paramsInstance, width);
                    m_editEntity = prev;
                }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "MaterialRefElement expect a MaterialHandle value!");
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