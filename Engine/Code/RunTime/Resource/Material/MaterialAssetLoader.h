#pragma once

#include <Base.h>

#include <Resource/Asset.h>
#include <Resource/AssetTypes.h>

namespace Spark { class FileSystem; }

namespace Spark::Resource
{
    //! Not BinaryAssetLoader: Compile dispatches on the raw's kind, which BinaryAssetData
    //! cannot state.
    class MaterialAssetLoader
    {
    public:
        UniquePtr<AssetData> Load(const AssetId& id, const FileSystem& fileSystem) const;
    };
}
