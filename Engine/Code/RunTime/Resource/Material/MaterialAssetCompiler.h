#pragma once

#include <EASTL/string_view.h>

#include <Base.h>

#include <Resource/Asset.h>
#include <Resource/AssetTypes.h>

namespace Spark::Resource
{
    class MaterialRawData;

    class MaterialAssetCompiler
    {
    public:
        //! Null on any failure. A `.smat` is authored, so a half-read material would be a
        //! silently wrong one.
        UniquePtr<AssetData> Compile(const AssetId& id, const MaterialRawData& raw) const;

        //! A cache entry written by WriteMaterialAsset. Null if it carries another identity.
        UniquePtr<AssetData> ReadCacheEntry(const uint8_t* bytes, size_t size,
                                            eastl::string_view identity) const;
    };
}
