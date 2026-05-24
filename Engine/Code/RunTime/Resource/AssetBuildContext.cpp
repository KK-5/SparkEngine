#include "AssetBuildContext.h"

#include <filesystem>


namespace Spark::Resource
{
    eastl::string AssetBuildContext::ResolvePath(eastl::string_view relative) const
    {
        if (relative.empty())
        {
            return {};
        }
        const eastl::string rel(relative.data(), relative.size());
        for (const auto& base : searchPaths)
        {
            std::filesystem::path full = std::filesystem::path(base.c_str()) / rel.c_str();
            if (std::filesystem::exists(full))
            {
                auto str = full.string();
                return eastl::string(str.c_str(), str.size());
            }
        }
        return {};
    }

    AssetBuildContext AssetBuildContext::MakeChild(AssetId subId, AssetType subType) const
    {
        AssetBuildContext child;
        child.id          = eastl::move(subId);
        child.type        = subType;
        child.parentId    = id;
        child.searchPaths = searchPaths;
        return child;
    }
}
