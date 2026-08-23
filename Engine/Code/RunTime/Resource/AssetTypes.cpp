#include "AssetTypes.h"

#include <Log/ILogSystem.h>

namespace Spark::Resource
{
    void ValidateAssetPath(const eastl::string& path)
    {
        if (path.empty())
        {
            return;
        }

        ASSERT(path.find("://") != eastl::string::npos,
            "[AssetId] '{}' is not a virtual path. Asset paths are mount://relative "
            "(engine://, project://, editor://, test://, sandbox://).", path.c_str());
    }
}
