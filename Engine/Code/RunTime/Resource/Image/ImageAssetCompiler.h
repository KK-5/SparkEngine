#pragma once

#include <Base.h>
#include <Resource/Asset.h>

#include "ImageAsset.h"

namespace Spark::Resource
{
    namespace VkFormatValue
    {
        constexpr uint32_t UNDEFINED                = 0;
        constexpr uint32_t R8_UNORM                 = 9;
        constexpr uint32_t R8G8_UNORM               = 16;
        constexpr uint32_t R8G8B8A8_UNORM           = 37;
        constexpr uint32_t R8G8B8A8_SRGB            = 43;
        constexpr uint32_t R32G32B32A32_SFLOAT      = 109;
        constexpr uint32_t BC1_RGBA_UNORM_BLOCK     = 133;
        constexpr uint32_t BC1_RGBA_SRGB_BLOCK      = 134;
        constexpr uint32_t BC3_UNORM_BLOCK          = 137;
        constexpr uint32_t BC3_SRGB_BLOCK           = 138;
        constexpr uint32_t BC4_UNORM_BLOCK          = 139;
        constexpr uint32_t BC4_SNORM_BLOCK          = 140;
        constexpr uint32_t BC5_UNORM_BLOCK          = 141;
        constexpr uint32_t BC5_SNORM_BLOCK          = 142;
        constexpr uint32_t BC6H_UFLOAT_BLOCK        = 143;
        constexpr uint32_t BC6H_SFLOAT_BLOCK        = 144;
        constexpr uint32_t BC7_UNORM_BLOCK          = 145;
        constexpr uint32_t BC7_SRGB_BLOCK           = 146;
    }

    class ImageAssetCompiler final : public AssetCompiler
    {
    public:
        ImageAssetCompiler() = default;
        ~ImageAssetCompiler() override = default;

        UniquePtr<AssetData> Compile(const AssetId& id, AssetData& rawData) override;

    private:
        static RHI::Format MapToRHIFormat(ImageFormat src, TextureCompression compression, ImageColorSpace colorSpace);

        eastl::vector<uint8_t> SerializeToKtx2(const ImageAssetData& data);
    };
}