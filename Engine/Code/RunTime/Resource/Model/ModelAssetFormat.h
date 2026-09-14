#pragma once

#include <EASTL/string_view.h>
#include <EASTL/vector.h>

#include <Base.h>

namespace Spark::Resource
{
    class AssetData;
    class ModelAssetData;

    //! A compiled model's cache payload, both halves. Native byte order: an entry never
    //! leaves the machine that cooked it.
    class ModelAssetFormat
    {
    public:
        //! Empty on failure.
        static eastl::vector<uint8_t> Write(const ModelAssetData& data, eastl::string_view identity);

        //! Null if the bytes are malformed or carry another identity.
        static UniquePtr<AssetData> Read(const uint8_t* bytes, size_t size, eastl::string_view identity);
    };
}
