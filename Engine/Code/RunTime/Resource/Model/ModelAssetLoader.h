#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Base.h>

#include "ModelAsset.h"

namespace fastgltf { class GltfDataBuffer; }

namespace Spark { class FileSystem; }

namespace Spark::Resource
{
    class ModelAssetLoader
    {
    public:
        UniquePtr<AssetData> Load(const AssetId& id, const FileSystem& fileSystem);

    private:
        UniquePtr<AssetData> LoadFromBuffer(class fastgltf::GltfDataBuffer& buf,
                                            eastl::string resolvedPath,
                                            const std::string& baseDir);
    };
}
