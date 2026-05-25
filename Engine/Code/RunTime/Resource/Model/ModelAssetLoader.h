#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Base.h>

#include "ModelAsset.h"

namespace fastgltf { class GltfDataBuffer; }

namespace Spark::Resource
{
    class ModelAssetLoader
    {
    public:
        void SetSearchPaths(const eastl::vector<eastl::string>& searchPaths);

        UniquePtr<AssetData> Load(const AssetId& id);

    private:
        eastl::string ResolvePath(const AssetId& id) const;

        UniquePtr<AssetData> LoadFromBuffer(class fastgltf::GltfDataBuffer& buf,
                                            eastl::string resolvedPath,
                                            const std::string& baseDir);

        eastl::vector<eastl::string> m_searchPaths;
    };
}
