#include "CommonAssetLoader.h"

#include <fstream>
#include <filesystem>

#include <Log/SpdLogSystem.h>

namespace Spark::Asset
{
    // ---- BinaryAssetData ----

    BinaryAssetData::BinaryAssetData(eastl::vector<uint8_t> bytes, eastl::string resolvedPath)
        : m_bytes(eastl::move(bytes))
        , m_resolvedPath(eastl::move(resolvedPath))
    {}

    // ---- BinaryAssetLoader ----

    BinaryAssetLoader::BinaryAssetLoader(const eastl::vector<eastl::string>& searchPaths)
        : m_searchPaths(searchPaths)
    {}

    eastl::unique_ptr<AssetData> BinaryAssetLoader::Load(const AssetId& id)
    {
        eastl::string path = ResolvePath(id);
        if (path.empty())
        {
            LOG_ERROR("Asset file not found: {}", id.GetName().GetStringView().data());
            return nullptr;
        }

        std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            LOG_ERROR("Failed to open asset file: {}", path.c_str());
            return nullptr;
        }

        auto size = file.tellg();
        file.seekg(0, std::ios::beg);

        eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(bytes.data()), size);

        return eastl::make_unique<BinaryAssetData>(eastl::move(bytes), eastl::move(path));
    }

    eastl::string BinaryAssetLoader::ResolvePath(const AssetId& id) const
    {
        auto name = id.GetName().GetStringView();
        for (const auto& searchPath : m_searchPaths)
        {
            std::filesystem::path full = std::filesystem::path(searchPath.c_str()) / name.data();
            if (std::filesystem::exists(full))
            {
                auto str = full.string();
                return eastl::string(str.c_str(), str.size());
            }
        }
        return {};
    }
}
