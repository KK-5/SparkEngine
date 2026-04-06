#include "ImageAsset.h"

#include <stb_image.h>
#include <Log/SpdLogSystem.h>

namespace Spark::Resource
{
    // ---- ImageAssetData ----

    ImageAssetData::ImageAssetData(int width, int height, ImageFormat format,
                                   eastl::vector<uint8_t> pixels, eastl::string resolvedPath)
        : m_width(width)
        , m_height(height)
        , m_format(format)
        , m_pixels(eastl::move(pixels))
        , m_resolvedPath(eastl::move(resolvedPath))
    {}

    uint32_t ImageAssetData::GetBytesPerPixel() const
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

    // ---- ImageAssetLoader ----

    ImageAssetLoader::ImageAssetLoader()
    {
        AssetCatalogBus::Handler::BusConnect();
    }

    ImageAssetLoader::~ImageAssetLoader()
    {
        AssetCatalogBus::Handler::BusDisconnect();
    }

    static ImageFormat ChannelsToFormat(int channels)
    {
        switch (channels)
        {
        case 1:  return ImageFormat::R8;
        case 2:  return ImageFormat::RG8;
        default: return ImageFormat::RGBA8;
        }
    }

    eastl::unique_ptr<AssetData> ImageAssetLoader::Load(const AssetId& id)
    {
        eastl::string path = ResolvePath(id);
        if (path.empty())
        {
            LOG_ERROR("Image file not found: {}", id.GetName().GetStringView().data());
            return nullptr;
        }

        int width = 0, height = 0, srcChannels = 0;

        if (stbi_is_hdr(path.c_str()))
        {
            // HDR 统一输出 RGBAF32，GPU 对 3 通道 float 支持不普遍
            float* data = stbi_loadf(path.c_str(), &width, &height, &srcChannels, 4);
            if (!data)
            {
                LOG_ERROR("Failed to load HDR image: {} ({})", path.c_str(), stbi_failure_reason());
                return nullptr;
            }

            const size_t byteCount = static_cast<size_t>(width) * height * 4 * sizeof(float);
            eastl::vector<uint8_t> pixels(byteCount);
            memcpy(pixels.data(), data, byteCount);
            stbi_image_free(data);

            return eastl::make_unique<ImageAssetData>(
                width, height, ImageFormat::RGBAF32, eastl::move(pixels), eastl::move(path));
        }
        else
        {
            // 先读文件头拿到源通道数，RGB8 在大多数 GPU 上没有原生格式支持，强制升为 RGBA8
            stbi_info(path.c_str(), &width, &height, &srcChannels);
            const int forceChannels = (srcChannels == 3) ? 4 : 0;
            uint8_t* data = stbi_load(path.c_str(), &width, &height, &srcChannels, forceChannels);
            if (!data)
            {
                LOG_ERROR("Failed to load image: {} ({})", path.c_str(), stbi_failure_reason());
                return nullptr;
            }

            const int actualChannels = (forceChannels != 0) ? forceChannels : srcChannels;
            const size_t byteCount = static_cast<size_t>(width) * height * actualChannels;
            eastl::vector<uint8_t> pixels(byteCount);
            memcpy(pixels.data(), data, byteCount);
            stbi_image_free(data);

            return eastl::make_unique<ImageAssetData>(
                width, height, ChannelsToFormat(actualChannels), eastl::move(pixels), eastl::move(path));
        }
    }

    // ---- ImageAsset ----

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

    ImageFormat ImageAsset::GetFormat() const
    {
        auto* data = GetImageData();
        return data ? data->GetFormat() : ImageFormat::RGBA8;
    }
}
