#include "CommonAssetLoader.h"

#include <filesystem>
#include <fstream>

#include <Log/ILogSystem.h>
#include <VFS/FileSystem.h>

namespace Spark::Resource
{
    // ---- BinaryAssetData ----

    BinaryAssetData::BinaryAssetData(eastl::vector<uint8_t> bytes, eastl::string resolvedPath)
        : m_bytes(eastl::move(bytes))
        , m_resolvedPath(eastl::move(resolvedPath))
    {}

    // ---- BinaryAssetLoader ----

    eastl::unique_ptr<AssetData> BinaryAssetLoader::LoadPhysicalFile(eastl::string resolvedPath) const
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

    eastl::unique_ptr<AssetData> BinaryAssetLoader::Load(const AssetId& id,
                                                        const FileSystem& fileSystem)
    {
        eastl::string path = fileSystem.ToPhysical(id.GetPath());
        if (path.empty())
        {
            return nullptr;
        }
        return LoadPhysicalFile(eastl::move(path));
    }
}
