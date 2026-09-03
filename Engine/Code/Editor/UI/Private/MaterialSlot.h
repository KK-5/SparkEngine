#pragma once

#include <cstdint>

#include <Reflection/RTTI.h>

namespace Editor
{
    //! The widget MaterialRefElement names: an object's material reference, drawn as two
    //! rows — the material it points at, and this object's override of it.
    //!
    //! Kept out of FieldWidgets.cpp because it is the only element that is a reference
    //! rather than a value: it follows the handle into the MaterialContext to name what it
    //! points at, and its second row edits a different component entirely.
    //!
    //! `worldEntity` must be the entity that owns the field — the override lives on the
    //! world side. Returns whether the user edited anything this frame.
    bool DrawMaterialSlot(const Spark::MetaType& owner, Spark::TypeId fieldId,
                          Spark::MetaData& data, Spark::MetaAny& instance,
                          float width, uint32_t worldEntity);
}
