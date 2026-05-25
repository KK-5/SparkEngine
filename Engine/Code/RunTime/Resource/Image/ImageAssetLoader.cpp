#include "ImageAssetLoader.h"

#include <filesystem>

#include <stb_image.h>

#include <Base.h>
#include <Log/SpdLogSystem.h>

namespace Spark::Resource
{
    static ImageFormat ChannelsToFormat(int channels)
    {
        switch (channels)
        {
        case 1:  return ImageFormat::R8;
        case 2:  return ImageFormat::RG8;
        default: return ImageFormat::RGBA8;
        }
    }

    static UniquePtr<AssetData> WrapLdrPixels(
        uint8_t* data, int w, int h, int srcCh, int forceCh,
        eastl::string&& resolvedPath)
    {
        if (!data)
        {
            LOG_ERROR("Failed to decode LDR image: {} ({})", resolvedPath.c_str(), stbi_failure_reason());
            return nullptr;
        }
        const int actualCh = (forceCh != 0) ? forceCh : srcCh;
        const size_t byteCount = static_cast<size_t>(w) * h * actualCh;
        eastl::vector<uint8_t> pixels(byteCount);
        memcpy(pixels.data(), data, byteCount);
        stbi_image_free(data);
        return MakeUnique<ImageAssetRawData>(
            w, h, ChannelsToFormat(actualCh), eastl::move(pixels), eastl::move(resolvedPath));
    }

    static UniquePtr<AssetData> WrapHdrPixels(
        float* data, int w, int h, eastl::string&& resolvedPath)
    {
        if (!data)
        {
            LOG_ERROR("Failed to decode HDR image: {} ({})", resolvedPath.c_str(), stbi_failure_reason());
            return nullptr;
        }
        const size_t byteCount = static_cast<size_t>(w) * h * 4 * sizeof(float);
        eastl::vector<uint8_t> pixels(byteCount);
        memcpy(pixels.data(), data, byteCount);
        stbi_image_free(data);
        return MakeUnique<ImageAssetRawData>(
            w, h, ImageFormat::RGBAF32, eastl::move(pixels), eastl::move(resolvedPath));
    }

    UniquePtr<AssetData> ImageAssetLoader::DecodeFromMemory(
        const uint8_t* bytes, size_t byteCount, eastl::string_view sourceLabel)
    {
        eastl::string label(sourceLabel.data(), sourceLabel.size());
        const auto* buf = reinterpret_cast<const stbi_uc*>(bytes);
        const int len = static_cast<int>(byteCount);
        int w = 0, h = 0, srcCh = 0;

        if (stbi_is_hdr_from_memory(buf, len))
        {
            return WrapHdrPixels(stbi_loadf_from_memory(buf, len, &w, &h, &srcCh, 4), w, h, eastl::move(label));
        }

        stbi_info_from_memory(buf, len, &w, &h, &srcCh);
        const int force = (srcCh == 3) ? 4 : 0;
        return WrapLdrPixels(
            stbi_load_from_memory(buf, len, &w, &h, &srcCh, force), w, h, srcCh, force, eastl::move(label));
    }

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

    UniquePtr<AssetData> ImageAssetLoader::Load(const AssetId& id)
    {
        eastl::string path = ResolvePath(id);
        if (path.empty())
        {
            LOG_ERROR("Image file not found: {}", id.GetPath().c_str());
            return nullptr;
        }

        int w = 0, h = 0, srcCh = 0;

        if (stbi_is_hdr(path.c_str()))
        {
            return WrapHdrPixels(stbi_loadf(path.c_str(), &w, &h, &srcCh, 4), w, h, eastl::move(path));
        }

        stbi_info(path.c_str(), &w, &h, &srcCh);
        const int force = (srcCh == 3) ? 4 : 0;
        return WrapLdrPixels(
            stbi_load(path.c_str(), &w, &h, &srcCh, force), w, h, srcCh, force, eastl::move(path));
    }
}
