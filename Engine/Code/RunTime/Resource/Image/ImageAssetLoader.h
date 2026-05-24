#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include "ImageAsset.h"

namespace Spark::Resource
{
    class ImageAssetLoader
    {
    public:
        ImageAssetLoader() = default;
        ~ImageAssetLoader() = default;

        void SetSearchPaths(const eastl::vector<eastl::string>& searchPaths)
        {
            m_searchPaths = searchPaths;
        }

        UniquePtr<AssetData> Load(const AssetId& id);

    private:
        eastl::string ResolvePath(const AssetId& id) const;

        eastl::vector<eastl::string> m_searchPaths;
    };
}