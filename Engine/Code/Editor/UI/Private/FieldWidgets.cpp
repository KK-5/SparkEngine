#include "FieldWidgets.h"

#include <imgui.h>

#include <ECS/Common.h>
#include <Math/Color.h>
#include <Math/Vector2.h>
#include <Math/Vector3.h>
#include <Math/Vector4.h>
#include <Resource/Asset.h>
#include <Resource/AssetJsonSerializer.h>          // AssetIdToDisplayString for the read-only slot
#include <Resource/AssetTypes.h>
#include <Resource/Image/ImageAsset.h>   // DescriptorForUsage / ImageUsage for texture slots
#include <Serialization/UIElement.h>
#include <Service/Service.h>
#include <Feature/UI/ImGui/IconManagerInterface.h>

#include "UI/Bus/AssetEditBus.h"

namespace Editor
{
    using namespace Spark;

    namespace
    {
        //! Share of a field row taken by its label. The rest is the widget.
        constexpr float kLabelWidthRatio = 0.38f;

        //! A few pixels so a full-width name does not touch the widget's frame.
        constexpr float kLabelGap = 4.f;

        //! Text cut to fit, with an ellipsis. Empty return = it already fits, so the caller
        //! can also read this as "was it shortened" and only offer a tooltip when it was.
        //!
        //! ImGui::Text is not bounded by SetNextItemWidth -- it paints at full length and
        //! the next widget's frame covers the overflow, which cuts mid-glyph and says
        //! nothing. Cutting it here is what makes the elision visible.
        eastl::string Ellipsize(const char* text, float maxWidth)
        {
            if (ImGui::CalcTextSize(text).x <= maxWidth)
            {
                return {};
            }

            eastl::string cut = text;
            while (!cut.empty() && ImGui::CalcTextSize((cut + "...").c_str()).x > maxWidth)
            {
                cut.pop_back();
            }
            return cut + "...";
        }

        //! An icon by virtual path. Opened once and remembered, and retried while the id is
        //! invalid: OpenIcon creates a world entity and a GPU image every call, so asking
        //! per frame would leak one of each per frame -- and a field can be drawn before the
        //! icon manager has anything to hand back, so a bad id must not be remembered.
        ImTextureID Icon(const char* path)
        {
            auto* iconManager = Service<UI::IconManagerInterface>::Get();
            if (!iconManager)
            {
                return ImTextureID_Invalid;
            }

            static eastl::unordered_map<eastl::string, Resource::AssetId> s_icons;

            Resource::AssetId& id = s_icons[path];
            if (!id.IsValid())
            {
                id = iconManager->OpenIcon(path);
                if (!id.IsValid())
                {
                    return ImTextureID_Invalid;
                }
            }
            return iconManager->RequestIconId(id);
        }
    }

    float FieldInputWidth(float width)
    {
        return width * (1.f - kLabelWidthRatio);
    }

    eastl::string BoxText(const eastl::string& value, float boxWidth, bool& elided)
    {
        const float textArea = boxWidth - ImGui::GetStyle().FramePadding.x * 2.f;

        const eastl::string shortened = Ellipsize(value.c_str(), textArea);
        elided = !shortened.empty();

        // InputText wants a writable, null-terminated buffer whose declared size counts the
        // terminator. Sized to the text rather than to a fixed 256 -- a display string longer
        // than that used to be strcpy'd straight past the end of the buffer.
        eastl::string buffer = elided ? shortened : value;
        buffer.resize(buffer.size() + 1);   // resize default-fills, i.e. terminates
        return buffer;
    }

    float BoxIconSlot()
    {
        return ImGui::GetFrameHeight();
    }

    bool DrawBoxIconButton(const char* id, const char* iconPath, const char* tooltip,
                           bool enabled)
    {
        // Taken before the InvisibleButton below, which becomes the last item.
        const ImVec2 boxMin = ImGui::GetItemRectMin();
        const ImVec2 boxMax = ImGui::GetItemRectMax();

        // The icon is drawn over the box, so the layout cursor has to be put back
        // afterwards -- an InvisibleButton advances it, and the next row would start under
        // the icon instead of on its own line.
        const ImVec2 rowEnd = ImGui::GetCursorScreenPos();

        const float  pad  = 3.f;
        const float  size = (boxMax.y - boxMin.y) - pad * 2.f;
        const ImVec2 iconMin(boxMax.x - size - pad, boxMin.y + pad);
        const ImVec2 iconMax(iconMin.x + size, iconMin.y + size);

        bool pressed = false;
        bool hovered = false;
        if (enabled)
        {
            ImGui::SetCursorScreenPos(iconMin);
            pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
            hovered = ImGui::IsItemHovered();
            ImGui::SetCursorScreenPos(rowEnd);
        }

        const ImTextureID icon = Icon(iconPath);
        if (icon != ImTextureID_Invalid)
        {
            ImU32 tint = IM_COL32(90, 90, 90, 255);          // disabled
            if (enabled)
            {
                tint = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(170, 170, 170, 255);
            }
            ImGui::GetWindowDrawList()->AddImage(icon, iconMin, iconMax,
                ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), tint);
        }

        if (hovered && tooltip)
        {
            ImGui::SetTooltip("%s", tooltip);
        }
        return pressed;
    }

    eastl::string DrawFieldLabel(float width, const char* label)
    {
        // Where this row starts, which is not the window's content edge -- callers indent
        // each field. Both the elision budget and the widget's column are measured from
        // here; an absolute SameLine would spend the indent out of the label's column and
        // leave the text to be painted over by exactly that much.
        const float rowStart   = ImGui::GetCursorPosX();
        const float labelWidth = width * kLabelWidthRatio;

        const eastl::string shortened = Ellipsize(label, labelWidth - kLabelGap);

        ImGui::AlignTextToFramePadding();
        // Unformatted: a field name is data, not a format string, and one containing '%'
        // would otherwise read arguments that were never passed.
        ImGui::TextUnformatted(shortened.empty() ? label : shortened.c_str());
        if (!shortened.empty() && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", label);
        }

        ImGui::SameLine(rowStart + labelWidth);
        ImGui::SetNextItemWidth(FieldInputWidth(width));
        return eastl::string("##") + label;
    }

    bool DrawFieldWidget(const MetaType& owner, TypeId fieldId, MetaData& data,
                         MetaAny& instance, float width, uint32_t editEntity)
    {
        const char* name = data.name();
        MetaCustom  uiElement = data.custom();
        MetaAny     fieldValue = data.get(instance);
        bool        changed = false;

        if (static_cast<EditTextElement*>(uiElement))
        {
            EditTextElement* ui = static_cast<EditTextElement*>(uiElement);
            if (eastl::string* value = fieldValue.try_cast<eastl::string>())
            {
                eastl::string buffer;
                buffer.resize(ui->maxLength);
                strcpy(buffer.data(), value->data());
                eastl::string label = DrawFieldLabel(width, name);
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), ui->readOnly ? ImGuiInputTextFlags_ReadOnly : 0))
                {
                    data.set(instance, eastl::string(buffer));
                    changed = true;
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
                eastl::string label = DrawFieldLabel(width, name);
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
                eastl::string label = DrawFieldLabel(width, name);
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::DragFloat(label.c_str(), value, ui->speed, ui->min, ui->max, ui->format.c_str()))
                {
                    data.set(instance, *value);
                    changed = true;
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
                eastl::string label = DrawFieldLabel(width, name);
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::SliderFloat(label.c_str(), value, ui->min, ui->max, ui->format.c_str()))
                {
                    data.set(instance, *value);
                    changed = true;
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "FloatElement expect a float value!");
            }
        }
        else if (static_cast<IntElement*>(uiElement))
        {
            IntElement* ui = static_cast<IntElement*>(uiElement);
            if (int* value = fieldValue.try_cast<int>())
            {
                eastl::string label = DrawFieldLabel(width, name);
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::DragInt(label.c_str(), value, ui->speed, ui->min, ui->max))
                {
                    data.set(instance, *value);
                    changed = true;
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "IntElement expect a int value!");
            }
        }
        else if (static_cast<IntSliderElement*>(uiElement))
        {
            IntSliderElement* ui = static_cast<IntSliderElement*>(uiElement);
            if (int* value = fieldValue.try_cast<int>())
            {
                eastl::string label = DrawFieldLabel(width, name);
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::SliderInt(label.c_str(), value, ui->min, ui->max))
                {
                    data.set(instance, *value);
                    changed = true;
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "IntSliderElement expect a int value!");
            }
        }
        else if (static_cast<UIntElement*>(uiElement))
        {
            UIntElement* ui = static_cast<UIntElement*>(uiElement);
            if (uint32_t* value = fieldValue.try_cast<uint32_t>())
            {
                eastl::string label = DrawFieldLabel(width, name);
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::DragScalar(label.c_str(), ImGuiDataType_U32, value, ui->speed, &ui->min, &ui->max))
                {
                    data.set(instance, *value);
                    changed = true;
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "UIntElement expect a uint32_t value!");
            }
        }
        else if (static_cast<UIntSliderElement*>(uiElement))
        {
            UIntSliderElement* ui = static_cast<UIntSliderElement*>(uiElement);
            if (uint32_t* value = fieldValue.try_cast<uint32_t>())
            {
                eastl::string label = DrawFieldLabel(width, name);
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::SliderScalar(label.c_str(), ImGuiDataType_U32, value, &ui->min, &ui->max))
                {
                    data.set(instance, *value);
                    changed = true;
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "UIntSliderElement expect a uint32_t value!");
            }
        }
        else if (static_cast<BoolElement*>(uiElement))
        {
            BoolElement* ui = static_cast<BoolElement*>(uiElement);
            if (bool* value = fieldValue.try_cast<bool>())
            {
                eastl::string label = DrawFieldLabel(width, name);
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::Checkbox(label.c_str(), value))
                {
                    data.set(instance, *value);
                    changed = true;
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "BoolElement expect a bool value!");
            }
        }
        else if (static_cast<Vec2Element*>(uiElement))
        {
            Vec2Element* ui = static_cast<Vec2Element*>(uiElement);
            if (Math::Vector2* value = fieldValue.try_cast<Math::Vector2>())
            {
                eastl::string label = DrawFieldLabel(width, name);
                float inputValue[2] = {value->x, value->y};
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::DragFloat2(label.c_str(), inputValue, ui->speed, ui->min, ui->max, ui->format.c_str()))
                {
                    Math::Vector2 vec2(inputValue[0], inputValue[1]);
                    data.set(instance, vec2);
                    changed = true;
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Vec2Element expect a Vector2 value!");
            }
        }
        else if (static_cast<Vec3Element*>(uiElement))
        {
            Vec3Element* ui = static_cast<Vec3Element*>(uiElement);
            if (Math::Vector3* value = fieldValue.try_cast<Math::Vector3>())
            {
                eastl::string label = DrawFieldLabel(width, name);
                float inputValue[3] = {value->x, value->y, value->z};
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::DragFloat3(label.c_str(), inputValue, ui->speed, ui->min, ui->max, ui->format.c_str()))
                {
                    Math::Vector3 vec3(inputValue[0], inputValue[1], inputValue[2]);
                    data.set(instance, vec3);
                    changed = true;
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Vec3Element expect a Vector3 value!");
            }
        }
        else if (static_cast<ColorElement*>(uiElement))
        {
            ColorElement* ui = static_cast<ColorElement*>(uiElement);
            if (Math::Color* value = fieldValue.try_cast<Math::Color>())
            {
                eastl::string label = DrawFieldLabel(width, name);
                float inputValue[4] = {value->r, value->g, value->b, value->a};
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (ImGui::ColorEdit4(label.c_str(), inputValue))
                {
                    Math::Color edited(inputValue[0], inputValue[1], inputValue[2], inputValue[3]);
                    data.set(instance, edited);
                    changed = true;
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "ColorElement expect a Color value!");
            }
        }
        else if (static_cast<AssetElement*>(uiElement))
        {
            AssetElement* ui = static_cast<AssetElement*>(uiElement);
            if (Resource::AssetId* value = fieldValue.try_cast<Resource::AssetId>())
            {
                const eastl::string display = Resource::AssetIdToDisplayString(*value);

                bool          elided = false;
                eastl::string buffer = BoxText(display, FieldInputWidth(width), elided);

                eastl::string label = DrawFieldLabel(width, name);
                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), ImGuiInputTextFlags_ReadOnly);
                if (ui->readOnly) { ImGui::EndDisabled(); }

                // AllowWhenDisabled: the box is disabled exactly when the id matters most
                // and cannot be edited.
                if (elided && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip("%s", display.c_str());
                }

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
                                static_cast<Spark::Entity>(editEntity), owner.id(), fieldId,
                                asset->GetAssetId(), asset->GetAssetType());
                            // The field itself is written later, once the asset is loaded.
                            // Reported as a change anyway: what the caller tracks with this
                            // is "the user edited something", and that already happened.
                            changed = true;
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
        else if (static_cast<TextureElement*>(uiElement))
        {
            TextureElement* ui = static_cast<TextureElement*>(uiElement);
            if (Resource::AssetId* value = fieldValue.try_cast<Resource::AssetId>())
            {
                const eastl::string display = Resource::AssetIdToDisplayString(*value);

                // Blender's shape: clearing the slot is an icon INSIDE the box, not a button
                // beside it. So a filled slot and an empty one are the same width, and the
                // row keeps one column edge instead of two. The icon covers the box's tail,
                // so the text gives up that much room rather than being painted under it.
                const bool showClear = !ui->readOnly && value->IsValid();

                bool          elided = false;
                eastl::string buffer = BoxText(display,
                    FieldInputWidth(width) - (showClear ? BoxIconSlot() : 0.f), elided);

                eastl::string label = DrawFieldLabel(width, name);

                if (ui->readOnly) { ImGui::BeginDisabled(true); }
                if (showClear) { ImGui::SetNextItemAllowOverlap(); }
                ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), ImGuiInputTextFlags_ReadOnly);

                // Asked for here, shown after EndDisabled below: the clear icon is another
                // item, and IsItemHovered only ever answers about the last one.
                const bool valueHovered =
                    elided && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);

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
                                static_cast<Spark::Entity>(editEntity), owner.id(), fieldId,
                                usedId, asset->GetAssetType());
                            changed = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Clear the slot back to None — a direct field write (no async load), on the
                // same commit path as the scalar editors (data.set -> ReplaceComponent).
                if (showClear)
                {
                    eastl::string clearId = "##Clear";
                    clearId += name;
                    if (DrawBoxIconButton(clearId.c_str(), "editor://x-square.svg", "Clear"))
                    {
                        data.set(instance, Resource::AssetId{});
                        changed = true;
                    }
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }

                // The icon's own tooltip is handled inside DrawBoxIconButton. AllowOverlap
                // makes the two mutually exclusive: with the mouse on the icon, the box is
                // no longer the hovered item.
                if (valueHovered)
                {
                    ImGui::SetTooltip("%s", display.c_str());
                }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "TextureElement expect an AssetId value!");
            }
        }
        else if (static_cast<EnumElement*>(uiElement))
        {
            // cast后类型不会被检测成enum，而是检测成int，这里要先保存下来，entt bug?
            MetaType enumType = fieldValue.type();
            EnumElement* ui = static_cast<EnumElement*>(uiElement);
            // 使用allow_cast检测是否允许转换
            if (fieldValue.allow_cast<int>())
            {
                int value = fieldValue.cast<int>();
                eastl::string label = DrawFieldLabel(width, name);
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
                    changed = true;
                }
                if (ui->readOnly) { ImGui::EndDisabled(); }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "EnumElement expect a enum value!");
            }
        }

        return changed;
    }

    bool DrawFieldWidgets(const MetaType& type, MetaAny& instance, float width, uint32_t editEntity)
    {
        bool changed = false;
        for (auto&& [id, data]: type.data())
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
            changed |= DrawFieldWidget(type, id, data, instance, width, editEntity);
        }
        return changed;
    }
}
