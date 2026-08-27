#pragma once

#include <Base.h>
#include <Serialization/Json.h>

#include "AssetTypes.h"

namespace Spark::Resource
{
    //! Asset references <-> JSON. Free functions rather than members of AssetManager: scene
    //! and material loading both need them, neither should have to reach a system singleton
    //! first, and neither direction consults the asset database.
    //!
    //! Requires Resource::Reflect to have been registered.

    //! ```json
    //! {"type":"Image","path":"project://Model/Furniture.glb","sub":"image/3",
    //!  "desc":{"usage":"NormalMap"}}
    //! ```
    //!
    //! `type` and `path` are always present; `sub` and `desc` are omitted when they carry
    //! nothing beyond the defaults. Fails on an id that names no asset.
    bool AssetIdToJson(const AssetId& id, JsonValue& out);

    //! Returns a default (invalid) AssetId if `type` or `path` is missing or unreadable.
    AssetId AssetIdFromJson(const JsonValue& in);

    //! AssetId's JsonOperation. One difference from the two above: an unset id is `null`
    //! rather than an error -- empty slots are the norm in a component. The read side needs
    //! a wrapper too, since AssetIdFromJson returns a default id on failure as well.
    bool AssetIdToJsonField(const AssetId& id, JsonValue& out);
    bool AssetIdFromJsonField(const JsonValue& in, AssetId& target);

    //! One-way, for logs and read-only inspector fields. Two ids that differ only in their
    //! descriptor -- the same texture as a normal map and as colour -- read alike here.
    eastl::string AssetIdToDisplayString(const AssetId& id);

    //! The type is passed in because the caller always has it -- writing, it comes off the
    //! AssetId; reading, it comes out of the file before the descriptor is even reached.
    bool DescriptorToJson(const AssetDescriptor& descriptor, AssetType type, JsonValue& out);

    //! Always returns a fresh descriptor, never one of the shared DefaultDescriptor
    //! singletons: filling one of those would rewrite the descriptor every existing AssetId
    //! of that type shares. Keys absent from `in` keep their default, so an empty object
    //! yields a plain default descriptor.
    Ptr<AssetDescriptor> DescriptorFromJson(AssetType type, const JsonValue& in);
}
