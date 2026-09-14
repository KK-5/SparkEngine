#include "CacheFormat.h"

namespace Spark::Resource
{
    CacheFormat GetCacheFormat(AssetType type)
    {
        switch (type)
        {
        case AssetType::Image:
            return {2, ".ktx2"};

        case AssetType::Model:
            return {1, ".smdl"};

        // Waits on a dependency mechanism: its includes are inputs the key cannot describe.
        case AssetType::Shader:
        // Material is different: it is not waiting on anything. A `.smat` is JSON whose
        // compiled form is barely more than a parse of itself, so an entry would cost a
        // file read to save a file read.
        case AssetType::Material:
        default:
            return {};
        }
    }
}
