#include "CommonAssetLoader.h"

#include <fstream>

#include <Resource/AssetBuildContext.h>

#include <Log/ILogSystem.h>

namespace Spark::Resource
{
    // ---- BinaryAssetData ----

    BinaryAssetData::BinaryAssetData(eastl::vector<uint8_t> bytes, eastl::string resolvedPath)
        : m_bytes(eastl::move(bytes))
        , m_resolvedPath(eastl::move(resolvedPath))
    {}

    // ---- BinaryAssetLoader ----

    eastl::string BinaryAssetLoader::ResolvePathStr(eastl::string_view path) const
    {
        return ResolveAssetPath(path, m_searchPaths);
    }

    eastl::string BinaryAssetLoader::ResolvePath(const AssetId& id) const
    {
        return ResolvePathStr(id.GetPath());
    }

    eastl::unique_ptr<AssetData> BinaryAssetLoader::ReadResolved(eastl::string resolvedPath) const
    {
        std::ifstream file(resolvedPath.c_str(), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            LOG_ERROR("Failed to open asset file: {}", resolvedPath.c_str());
            return nullptr;
        }

        auto size = file.tellg();
        file.seekg(0, std::ios::beg);

        eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(bytes.data()), size);

        return eastl::make_unique<BinaryAssetData>(eastl::move(bytes), eastl::move(resolvedPath));
    }

    eastl::unique_ptr<AssetData> BinaryAssetLoader::Load(const AssetId& id)
    {
        eastl::string path = ResolvePath(id);
        if (path.empty())
        {
            LOG_ERROR("Asset file not found: {}", id.GetPath().c_str());
            return nullptr;
        }
        return ReadResolved(eastl::move(path));
    }

    eastl::unique_ptr<AssetData> BinaryAssetLoader::LoadFile(eastl::string_view path) const
    {
        eastl::string resolved = ResolvePathStr(path);
        if (resolved.empty())
        {
            return nullptr;
        }
        return ReadResolved(eastl::move(resolved));
    }
}
