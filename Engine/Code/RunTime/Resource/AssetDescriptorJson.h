#pragma once

#include <Base.h>
#include <Serialization/Json.h>

#include "AssetTypes.h"

namespace Spark::Resource
{
    //! Descriptor <-> JSON. Free functions rather than members of AssetManager: scene and
    //! material loading both need them, neither should have to reach a system singleton
    //! first, and neither direction consults the asset database.
    //!
    //! The type is passed in because the caller always has it -- writing, it comes off the
    //! AssetId; reading, it comes out of the file before the descriptor is even reached.
    //! Requires Resource::Reflect to have been registered.

    bool DescriptorToJson(const AssetDescriptor& descriptor, AssetType type, JsonValue& out);

    //! Always returns a fresh descriptor, never one of the shared DefaultDescriptor
    //! singletons: filling one of those would rewrite the descriptor every existing AssetId
    //! of that type shares. Keys absent from `in` keep their default, so an empty object
    //! yields a plain default descriptor.
    Ptr<AssetDescriptor> DescriptorFromJson(AssetType type, const JsonValue& in);
}
