#include "ImageAsset.h"

#include <Log/SpdLogSystem.h>

namespace Spark::Resource
{
    // ---- ImageAssetData ----

    ImageAssetRawData::ImageAssetRawData(int width, int height, ImageFormat format,
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

    ImageAsset::ImageAsset(AssetId id)
        : Asset(eastl::move(id), AssetType::Image)
    {}

    const ImageAssetRawData* ImageAsset::GetImageRawData() const
    {
        return GetData<ImageAssetRawData>();
    }

    int ImageAsset::GetWidth() const
    {
        auto* data = GetImageRawData();
        return data ? data->GetWidth() : 0;
    }

    int ImageAsset::GetHeight() const
    {
        auto* data = GetImageRawData();
        return data ? data->GetHeight() : 0;
    }

    ImageFormat ImageAsset::GetImageFormat() const
    {
        auto* data = GetImageRawData();
        return data ? data->GetFormat() : ImageFormat::RGBA8;
    }
}
