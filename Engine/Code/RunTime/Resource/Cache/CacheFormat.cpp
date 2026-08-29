#include "CacheFormat.h"

namespace Spark::Resource
{
    CacheFormat GetCacheFormat(AssetType type)
    {
        switch (type)
        {
        case AssetType::Image:
            return {2, ".ktx2"};

        // Shader waits on a dependency mechanism, Model on that plus the sub-asset
        // unification -- both have inputs the key cannot describe.
        case AssetType::Shader:
        case AssetType::Model:
        // Material is different: it is not waiting on anything. A `.smat` is JSON whose
        // compiled form is barely more than a parse of itself, so an entry would cost a
        // file read to save a file read.
        case AssetType::Material:
        default:
            return {};
        }
    }
}
