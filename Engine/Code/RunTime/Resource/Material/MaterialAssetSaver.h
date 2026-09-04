#pragma once

#include <cstdint>

#include <EASTL/string_view.h>

namespace Spark::Resource
{
    class AssetId;
    class MaterialAssetData;

    enum class MaterialSaveResult : uint8_t
    {
        Ok = 0,

        //! A texture slot holds a sub-asset id: the `.smat` works this session and loses its
        //! textures the next, when ProcessAsset refuses to build a sub-asset alone. Its own
        //! value because it is the one outcome the user can act on.
        EmbeddedTexture,

        //! Everything else; the reason is in the log and there is nothing to act on.
        Failed,
    };

    //! A material's values as a `.smat` at `virtualPath`, registered by the time this
    //! returns. `out` is untouched unless the result is Ok.
    //!
    //! The seam is values-in/asset-out rather than one level down at the bytes because what
    //! happens between grows: an embedded texture is refused today, extracted here later
    //! (and `data` turns non-const then, since extraction rewrites the ids it saved).
    MaterialSaveResult SaveMaterialAsset(const MaterialAssetData& data,
                                         eastl::string_view virtualPath, AssetId& out);
}
