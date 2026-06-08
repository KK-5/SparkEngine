#include "AssetBuildContext.h"

#include <filesystem>


namespace Spark::Resource
{
    eastl::string ResolveAssetPath(eastl::string_view path,
                                    const eastl::vector<eastl::string>& searchPaths)
    {
        namespace fs = std::filesystem;

        if (path.empty())
        {
            return {};
        }

        // Check if path exists as-is (full / canonical path)
        {
            std::error_code ec;
            if (fs::exists(path.data(), ec))
            {
                auto str = fs::path(path.data()).generic_string();
                return eastl::string(str.c_str(), str.size());
            }
        }

        // Search through registered paths
        for (const auto& sp : searchPaths)
        {
            fs::path full = fs::path(sp.c_str()) / path.data();
            std::error_code ec;
            if (fs::exists(full, ec))
            {
                auto str = full.generic_string();
                return eastl::string(str.c_str(), str.size());
            }
        }
        return {};
    }

    eastl::string AssetBuildContext::ResolvePath(eastl::string_view relative) const
    {
        return ResolveAssetPath(relative, searchPaths);
    }

    AssetBuildContext AssetBuildContext::MakeChild(AssetId subId, AssetType subType) const
    {
        AssetBuildContext child;
        child.id          = eastl::move(subId);
        child.type        = subType;
        child.parentId    = id;
        child.searchPaths = searchPaths;
        child.db          = db;
        return child;
    }
}
