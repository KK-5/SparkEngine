#pragma once

#include <cstdint>

#include <Resource/AssetTypes.h>

namespace Spark::Resource
{
    //! One asset type's on-disk form. Deliberately not a universal container: an image is
    //! written as a plain `.ktx2` any texture viewer opens.
    //!
    //! `version == 0` means the type has no cache yet, and is the whole opt-in.
    struct CacheFormat
    {
        uint32_t    version{0};
        const char* extension{nullptr};
    };

    //! Bump `version` in the same commit that changes a builder's output. It feeds the cache
    //! key, so every existing entry of that type becomes unreachable rather than wrong.
    CacheFormat GetCacheFormat(AssetType type);
}
