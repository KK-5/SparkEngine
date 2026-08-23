#include "AssetTypes.h"

#include <Log/ILogSystem.h>

namespace Spark::Resource
{
    void ValidateAssetId(const eastl::string& path, AssetType type)
    {
        if (path.empty())
        {
            return;
        }

        ASSERT(path.find("://") != eastl::string::npos,
            "[AssetId] '{}' is not a virtual path. Asset paths are mount://relative "
            "(engine://, project://, editor://, test://, sandbox://).", path.c_str());

        ASSERT(type != AssetType::Unknown,
            "[AssetId] '{}' has no asset type. Build ids through Of<T>/OfSub<T>, or pass a "
            "concrete AssetType to the value-typed Of.", path.c_str());
    }

    void ValidateAssetType(const AssetId& id, AssetType expected)
    {
        ASSERT(id.GetAssetType() == expected,
            "[AssetId] '{}' is a {} asset, requested as {}.", id.GetPath().c_str(),
            static_cast<uint32_t>(id.GetAssetType()), static_cast<uint32_t>(expected));
    }
}
