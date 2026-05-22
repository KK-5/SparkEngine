#pragma once

#include "ImageAsset.h"

namespace Spark::Resource
{
    class ImageAssetLoader : public AssetLoader
    {
    public:
        ImageAssetLoader();
        ~ImageAssetLoader();

       UniquePtr<AssetData> Load(const AssetId& id) override;
    };
}