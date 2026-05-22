#pragma once

#include <EASTL/unordered_map.h>

#include <Base.h>
#include <Resource/Asset.h>

#include "ImageAsset.h"

namespace Spark::Resource
{
    class ImageAssetCompiler final : public AssetCompiler
    {
    public:
        ImageAssetCompiler() = default;
        ~ImageAssetCompiler() override = default;

        UniquePtr<AssetData> Compile(const AssetId& id, AssetData& rawData) override;

        void SetCompileDescriptor(const AssetId& id, const ImageCompileDescriptor& desc);
    
    private:
        ImageCompileDescriptor GetDescriptor(const AssetId& id) const;

        static RHI::Format MapToRHIFormat(ImageFormat src, ImageColorSpace cs);

        eastl::unordered_map<AssetId, ImageCompileDescriptor> m_descriptors;
    };
}