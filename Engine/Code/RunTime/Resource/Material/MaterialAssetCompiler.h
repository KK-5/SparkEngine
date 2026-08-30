#pragma once

#include <EASTL/unique_ptr.h>

#include <Base.h>

#include <Resource/Asset.h>
#include <Resource/AssetTypes.h>

namespace Spark::Resource
{
    //! Parses a `.smat` into a MaterialAssetData. Held by MaterialAssetBuilder as a helper,
    //! the same split every other asset type uses.
    class MaterialAssetCompiler
    {
    public:
        //! Null on any failure: malformed JSON, a shading model this build cannot express,
        //! or a value that does not fit the field it names. A `.smat` is authored, not
        //! generated, so a half-read material would be a silently wrong one.
        UniquePtr<AssetData> Compile(const AssetId& id, const uint8_t* bytes, size_t size) const;
    };
}
