#pragma once

#include <cstdint>

#include <EASTL/string.h>

#include <Reflection/RTTI.h>
#include <Resource/AssetTypes.h>

namespace Spark::Resource
{
    class Asset;
}

namespace Editor
{
    //! The asset dropped on the item submitted just before this call, or nullptr if there
    //! is no drop this frame or the payload is not of `expected`. AssetType::Unknown takes
    //! any type. Opens and closes the drop target itself.
    //!
    //! Must follow its target item immediately -- ImGui derives the drop target from the
    //! last item submitted.
    //!
    //! This is the only place that knows the payload carries a raw Asset*: AssetId has
    //! non-trivial members and ImGui copies payloads with memcpy, so a pointer is the only
    //! thing that can travel in one.
    const Spark::Resource::Asset* AcceptAssetDrop(Spark::Resource::AssetType expected);

    //! The two-column layout every field shares: label on the left, widget on the right.
    //! Returns the "##label" ImGui id for the widget that follows. Exported because the
    //! material slot in Component View draws its own rows and has to line up with these.
    eastl::string DrawFieldLabel(float width, const char* label);

    //! What DrawFieldLabel leaves for the widget. A row that puts something else beside
    //! its widget -- a button -- takes that width out of this, or the extra item is laid
    //! out past the column's edge and clipped away.
    float FieldInputWidth(float width);

    //! The text a read-only box actually shows, elided to `boxWidth` and terminated so it
    //! can go straight to InputText. InputText paints to the box's edge and knows nothing
    //! about what is drawn over it, so a value that does not fit has to be cut first.
    //! `elided` reports whether it was, which is also when a tooltip is worth offering.
    eastl::string BoxText(const eastl::string& value, float boxWidth, bool& elided);

    //! Width an in-box icon takes at the box's right end. Subtract it from the box width
    //! passed to BoxText, or the text runs under the icon.
    float BoxIconSlot();

    //! An icon button drawn INSIDE the box submitted just before it, right-aligned --
    //! Blender's shape for a per-field action. Returns whether it was clicked.
    //!
    //! Call it immediately after the box's widget, and put SetNextItemAllowOverlap() before
    //! that widget: hovering the box makes the box the hovered id, and a later item over the
    //! same pixels is refused hover unless the one underneath opted into being overlapped.
    //!
    //! `iconPath` is a virtual path (`editor://x-square.svg`); it is opened once and kept.
    bool DrawBoxIconButton(const char* id, const char* iconPath, const char* tooltip,
                           bool enabled = true);

    //! Draws one reflected field with the widget its UIElement names, committing an edit
    //! straight into `instance`. Returns whether the user changed the value this frame.
    //!
    //! `owner` / `fieldId` / `editEntity` address the field for an asset drop: a drop only
    //! puts identity on AssetEditBus, and the write lands later, once the asset is loaded,
    //! through whatever context the owning component type is bound to. `editEntity` is a raw
    //! uint32 for the same reason the reflected component ops take one -- it may be a world
    //! Entity or a MaterialHandle, and nothing here needs to know which. The exception is
    //! MaterialRefElement (see MaterialSlot.h), which only appears on world components.
    bool DrawFieldWidget(const Spark::MetaType& owner, Spark::TypeId fieldId,
                         Spark::MetaData& data, Spark::MetaAny& instance,
                         float width, uint32_t editEntity);

    //! Every field of `type`, in registration order. Returns whether any of them changed.
    bool DrawFieldWidgets(const Spark::MetaType& type, Spark::MetaAny& instance,
                          float width, uint32_t editEntity);
}
