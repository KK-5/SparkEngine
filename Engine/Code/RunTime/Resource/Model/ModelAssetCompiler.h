#pragma once

#include <Base.h>
#include <Resource/Asset.h>

namespace Spark::Resource
{
    class ModelAssetCompiler final
    {
    public:
        ModelAssetCompiler() = default;
        ~ModelAssetCompiler() = default;

        UniquePtr<AssetData> Compile(const AssetId& id, AssetData& rawData);
    };
}
