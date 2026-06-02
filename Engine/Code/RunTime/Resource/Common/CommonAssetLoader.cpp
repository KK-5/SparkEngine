#include "CommonAssetLoader.h"

#include <filesystem>
#include <fstream>

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
        namespace fs = std::filesystem;
        eastl::string p(path.data(), path.size());

        // 1) Literal path (absolute, or relative to CWD) — handles include
        //    paths DXC already resolved against its -I directories.
        if (fs::exists(p.c_str()))
        {
            auto str = fs::path(p.c_str()).string();
            return eastl::string(str.c_str(), str.size());
        }

        // 2) Relative to each search path — handles asset-relative paths.
        for (const auto& searchPath : m_searchPaths)
        {
            fs::path full = fs::path(searchPath.c_str()) / p.c_str();
            if (fs::exists(full))
            {
                auto str = full.string();
                return eastl::string(str.c_str(), str.size());
            }
        }
        return {};
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
