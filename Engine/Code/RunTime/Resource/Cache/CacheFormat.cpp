#include "CacheFormat.h"

namespace Spark::Resource
{
    CacheFormat GetCacheFormat(AssetType type)
    {
        switch (type)
        {
        case AssetType::Image:
            return {1, ".ktx2"};

        // Shader waits on a dependency mechanism, Model on that plus the sub-asset
        // unification -- both have inputs the key cannot describe.
        case AssetType::Shader:
        case AssetType::Model:
        default:
            return {};
        }
    }
}
