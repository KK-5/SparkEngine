#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Base.h>

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

        static UniquePtr<AssetData> DecodeFromMemory(
            const uint8_t* bytes, size_t byteCount, eastl::string_view sourceLabel);

    private:
        eastl::string ResolvePath(const AssetId& id) const;

        eastl::vector<eastl::string> m_searchPaths;
    };
}
