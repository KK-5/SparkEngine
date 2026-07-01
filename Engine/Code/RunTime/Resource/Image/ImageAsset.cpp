#include "ImageAsset.h"

#include <EASTLEX/hash.h>
#include <Log/ILogSystem.h>

namespace Spark::Resource
{
    // ---- ImageAssetDescriptor ----

    AssetHash ImageAssetDescriptor::Hash() const
    {
        size_t h = 0;
        eastl::hash_combine(h, static_cast<size_t>(compression));
        eastl::hash_combine(h, static_cast<size_t>(colorSpace));
        eastl::hash_combine(h, static_cast<size_t>(maxMipLevels));
        eastl::hash_combine(h, static_cast<size_t>(usage));
        // faceSize only matters for the cubemap path; folding it unconditionally
        // would make two otherwise-identical Texture2D descriptors hash apart.
        if (usage == ImageUsage::EnvironmentCubemap)
        {
            eastl::hash_combine(h, static_cast<size_t>(cubemapFaceSize));
        }
        return static_cast<AssetHash>(h);
    }

    // ---- ImageAssetData ----

    ImageAssetRawData::ImageAssetRawData(uint32_t width, uint32_t height, ImageFormat format,
                                   eastl::vector<uint8_t> pixels, eastl::string resolvedPath)
        : m_width(width)
        , m_height(height)
        , m_format(format)
        , m_pixels(eastl::move(pixels))
        , m_resolvedPath(eastl::move(resolvedPath))
    {}

    uint32_t ImageAssetRawData::GetBytesPerPixel() const
    {
        switch (m_format)
        {
        case ImageFormat::R8:      return 1;
        case ImageFormat::RG8:     return 2;
        case ImageFormat::RGBA8:   return 4;
        case ImageFormat::RGBAF32: return 16;
        default:                   return 4;
        }
    }

    // ---- ImageAsset ----

    Ptr<AssetDescriptor> ImageAsset::DefaultDescriptor()
    {
        static Ptr<AssetDescriptor> instance(new ImageAssetDescriptor{});
        return instance;
    }

    Ptr<AssetDescriptor> ImageAsset::DefaultHDRDescriptor()
    {
        static Ptr<AssetDescriptor> instance = []
        {
            auto* desc = new ImageAssetDescriptor{};
            desc->usage           = ImageUsage::EnvironmentCubemap;
            desc->colorSpace      = ImageColorSpace::Linear;
            desc->compression     = TextureCompression::None;
            desc->cubemapFaceSize = 0; // auto: derived from the source at compile
            return Ptr<AssetDescriptor>(desc);
        }();
        return instance;
    }

    ImageAsset::ImageAsset(AssetId id)
        : Asset(eastl::move(id), AssetType::Image)
    {}

    const ImageAssetData* ImageAsset::GetImageData() const
    {
        return GetData<ImageAssetData>();
    }

    int ImageAsset::GetWidth() const
    {
        auto* data = GetImageData();
        return data ? data->GetWidth() : 0;
    }

    int ImageAsset::GetHeight() const
    {
        auto* data = GetImageData();
        return data ? data->GetHeight() : 0;
    }

    uint32_t ImageAsset::GetMipLevels() const
    {
        auto* data = GetImageData();
        return data ? data->GetMipLevels() : 0;
    }

    RHI::Format ImageAsset::GetFormat() const
    {
        auto* data = GetImageData();
        return data ? data->GetFormat() : RHI::Format::Unknown;
    }

    eastl::string_view ImageAsset::GetPath() const
    {
        return GetAssetId().GetPath();
    }
}
