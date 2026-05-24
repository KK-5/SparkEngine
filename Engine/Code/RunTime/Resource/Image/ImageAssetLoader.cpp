#include "ImageAssetLoader.h"

#include <filesystem>

#include <stb_image.h>

#include <Base.h>
#include <Log/SpdLogSystem.h>

namespace Spark::Resource
{
    eastl::string ImageAssetLoader::ResolvePath(const AssetId& id) const
    {
        const eastl::string& path = id.GetPath();
        for (const auto& searchPath : m_searchPaths)
        {
            std::filesystem::path full = std::filesystem::path(searchPath.c_str()) / path.c_str();
            if (std::filesystem::exists(full))
            {
                auto str = full.string();
                return eastl::string(str.c_str(), str.size());
            }
        }
        return {};
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

    UniquePtr<AssetData> ImageAssetLoader::Load(const AssetId& id)
    {
        eastl::string path = ResolvePath(id);
        if (path.empty())
        {
            LOG_ERROR("Image file not found: {}", id.GetPath().c_str());
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

            return MakeUnique<ImageAssetRawData>(
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

            return MakeUnique<ImageAssetRawData>(
                width, height, ChannelsToFormat(actualChannels), eastl::move(pixels), eastl::move(path));
        }
    }
}