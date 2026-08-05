#include "ImageAssetLoader.h"

#include <cstdio>
#include <filesystem>

#include <Resource/AssetBuildContext.h>

#include <stb_image.h>
#include <nanosvg.h>
#include <nanosvgrast.h>
#include <ktx.h>

#include <Base.h>
#include <Log/ILogSystem.h>

namespace Spark::Resource
{
    namespace
    {
        // Mirror of MapToVkFormat in ImageAssetCompiler.cpp. The two will merge when the
        // asset cache gives reading and writing a shared home; until then keep them in
        // step by hand. Unlisted formats are rejected, never guessed.
        RHI::Format MapVkFormatToRHI(uint32_t vkFormat)
        {
            switch (vkFormat)
            {
                case 9:   return RHI::Format::R8_UNORM;
                case 16:  return RHI::Format::R8G8_UNORM;
                case 37:  return RHI::Format::R8G8B8A8_UNORM;
                case 43:  return RHI::Format::R8G8B8A8_UNORM_SRGB;
                case 83:  return RHI::Format::R16G16_FLOAT;
                case 109: return RHI::Format::R32G32B32A32_FLOAT;
                case 133: return RHI::Format::BC1_UNORM;
                case 134: return RHI::Format::BC1_UNORM_SRGB;
                case 137: return RHI::Format::BC3_UNORM;
                case 138: return RHI::Format::BC3_UNORM_SRGB;
                case 139: return RHI::Format::BC4_UNORM;
                case 140: return RHI::Format::BC4_SNORM;
                case 141: return RHI::Format::BC5_UNORM;
                case 142: return RHI::Format::BC5_SNORM;
                case 143: return RHI::Format::BC6H_UF16;
                case 144: return RHI::Format::BC6H_SF16;
                case 145: return RHI::Format::BC7_UNORM;
                case 146: return RHI::Format::BC7_UNORM_SRGB;
                default:  return RHI::Format::Unknown;
            }
        }
    }

    bool IsCompiledImagePath(eastl::string_view path)
    {
        constexpr eastl::string_view kExt = ".ktx2";
        return path.size() > kExt.size()
            && path.compare(path.size() - kExt.size(), kExt.size(), kExt) == 0;
    }

static UniquePtr<AssetData> DecodeSvg(
    const char* svgData, int w, int h, eastl::string&& resolvedPath)
{
    NSVGimage* image = nsvgParse(const_cast<char*>(svgData), "px", 96.0f);
    if (!image)
    {
        LOG_ERROR("Failed to parse SVG: {}", resolvedPath.c_str());
        return nullptr;
    }

    const int rasterW = (w > 0 && h > 0) ? w : 256;
    const int rasterH = (w > 0 && h > 0) ? h : 256;

    size_t byteCount = static_cast<size_t>(rasterW) * rasterH * 4;
    eastl::vector<uint8_t> pixels(byteCount);

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, image, 0, 0,
                  static_cast<float>(rasterW) / image->width,
                  pixels.data(), rasterW, rasterH, rasterW * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    return MakeUnique<ImageAssetRawData>(
        rasterW, rasterH, ImageFormat::RGBA8, eastl::move(pixels), eastl::move(resolvedPath));
}
    // Every LDR source decodes to 4 channels regardless of how it was encoded. A grayscale
    // PNG/JPEG is the encoder's size optimization, not a claim that the image carries one
    // channel -- keeping it at one would sample as (r, 0, 0, 1) and turn a white base color
    // red. Channel count is a property of the slot the texture feeds, not of the file.
    static constexpr int kLdrChannels = 4;

    static UniquePtr<AssetData> WrapLdrPixels(
        uint8_t* data, int w, int h, eastl::string&& resolvedPath)
    {
        if (!data)
        {
            LOG_ERROR("Failed to decode LDR image: {} ({})", resolvedPath.c_str(), stbi_failure_reason());
            return nullptr;
        }
        const size_t byteCount = static_cast<size_t>(w) * h * kLdrChannels;
        eastl::vector<uint8_t> pixels(byteCount);
        memcpy(pixels.data(), data, byteCount);
        stbi_image_free(data);
        return MakeUnique<ImageAssetRawData>(
            w, h, ImageFormat::RGBA8, eastl::move(pixels), eastl::move(resolvedPath));
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
            // stbi_loadf writes w/h via the out-params; it MUST be sequenced before w/h
            // are read as arguments. Hoist it to its own statement — passing it inline
            // alongside w,h relies on unspecified argument evaluation order (MSVC reads
            // right-to-left, so w,h would be read as 0 before stbi runs).
            float* pixels = stbi_loadf_from_memory(buf, len, &w, &h, &srcCh, 4);
            return WrapHdrPixels(pixels, w, h, eastl::move(label));
        }

        stbi_uc* pixels = stbi_load_from_memory(buf, len, &w, &h, &srcCh, kLdrChannels);
        return WrapLdrPixels(pixels, w, h, eastl::move(label));
    }

    eastl::string ImageAssetLoader::ResolvePath(const AssetId& id) const
    {
        return ResolveAssetPath(id.GetPath(), m_searchPaths);
    }

    UniquePtr<AssetData> ImageAssetLoader::LoadKtx2(const eastl::string& path)
    {
        ktxTexture2*   tex = nullptr;
        KTX_error_code res = ktxTexture2_CreateFromNamedFile(
            path.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &tex);
        if (res != KTX_SUCCESS)
        {
            LOG_ERROR("[ImageAssetLoader] Failed to open KTX2 {}: {}", path.c_str(), static_cast<int>(res));
            return nullptr;
        }

        // Cube / array containers are deliberately out of scope: KTX2 draws a numFaces vs
        // numLayers distinction this engine's writer does not yet honour, so accepting them
        // here would mean inventing a convention the write side disagrees with.
        if (tex->numDimensions != 2 || tex->numLayers != 1 || tex->numFaces != 1
            || tex->isArray || tex->isCubemap)
        {
            LOG_ERROR("[ImageAssetLoader] {}: only 2D single-layer KTX2 is supported "
                      "(dims={}, layers={}, faces={}).",
                      path.c_str(), tex->numDimensions, tex->numLayers, tex->numFaces);
            ktxTexture_Destroy(ktxTexture(tex));
            return nullptr;
        }
        if (tex->supercompressionScheme != KTX_SS_NONE)
        {
            LOG_ERROR("[ImageAssetLoader] {}: supercompressed KTX2 needs transcoding, "
                      "which is not implemented.", path.c_str());
            ktxTexture_Destroy(ktxTexture(tex));
            return nullptr;
        }

        const RHI::Format format = MapVkFormatToRHI(tex->vkFormat);
        if (format == RHI::Format::Unknown)
        {
            LOG_ERROR("[ImageAssetLoader] {}: no RHI::Format for vkFormat {}.",
                      path.c_str(), tex->vkFormat);
            ktxTexture_Destroy(ktxTexture(tex));
            return nullptr;
        }

        auto result = MakeUnique<ImageAssetData>();
        result->m_width       = tex->baseWidth;
        result->m_height      = tex->baseHeight;
        result->m_mipLevels   = tex->numLevels;
        result->m_arrayLayers = 1;
        result->m_format      = format;

        // Level by level, ascending, tightly packed -- the order AsyncUploadSystem walks.
        // Not a single memcpy of pData: KTX2 stores levels smallest-first and may pad
        // between them.
        uint64_t total = 0;
        for (uint32_t level = 0; level < tex->numLevels; ++level)
        {
            total += ktxTexture_GetImageSize(ktxTexture(tex), level);
        }
        result->m_textureBytes.resize(total);
        result->m_mips.reserve(tex->numLevels);

        uint64_t dstOffset = 0;
        for (uint32_t level = 0; level < tex->numLevels; ++level)
        {
            ktx_size_t srcOffset = 0;
            res = ktxTexture_GetImageOffset(ktxTexture(tex), level, 0, 0, &srcOffset);
            if (res != KTX_SUCCESS)
            {
                LOG_ERROR("[ImageAssetLoader] {}: GetImageOffset level {} failed: {}",
                          path.c_str(), level, static_cast<int>(res));
                ktxTexture_Destroy(ktxTexture(tex));
                return nullptr;
            }

            const uint64_t levelBytes = ktxTexture_GetImageSize(ktxTexture(tex), level);
            memcpy(result->m_textureBytes.data() + dstOffset, tex->pData + srcOffset, levelBytes);
            result->m_mips.push_back({dstOffset, levelBytes});
            dstOffset += levelBytes;
        }

        ktxTexture_Destroy(ktxTexture(tex));

        LOG_INFO("[ImageAssetLoader] {}: {}x{}, {} mips, format={}, {}B (already compiled)",
                 path.c_str(), result->m_width, result->m_height, result->m_mipLevels,
                 static_cast<int>(result->m_format), result->m_textureBytes.size());
        return result;
    }

    UniquePtr<AssetData> ImageAssetLoader::Load(const AssetId& id, bool& outIsCompiled)
    {
        outIsCompiled = false;

        eastl::string path = ResolvePath(id);
        if (path.empty())
        {
            LOG_ERROR("[ImageAssetLoader] Image file not found: {}", id.GetPath().c_str());
            return nullptr;
        }

        if (IsCompiledImagePath(path))
        {
            UniquePtr<AssetData> compiled = LoadKtx2(path);
            outIsCompiled = compiled != nullptr;
            return compiled;
        }

        // SVG: read file into string and rasterize via nanosvg
        if (path.size() > 4 && path.compare(path.size() - 4, 4, ".svg") == 0)
        {
            std::error_code ec;
            const auto fsize = std::filesystem::file_size(std::filesystem::path(path.c_str()), ec);
            if (ec || fsize == 0)
            {
                LOG_ERROR("Failed to read SVG file: {}", path.c_str());
                return nullptr;
            }
            eastl::string svgData(static_cast<size_t>(fsize) + 1, '\0');
            FILE* f = fopen(path.c_str(), "rb");
            if (!f)
            {
                LOG_ERROR("Failed to open SVG file: {}", path.c_str());
                return nullptr;
            }
            fread(svgData.data(), 1, static_cast<size_t>(fsize), f);
            fclose(f);
            return DecodeSvg(svgData.c_str(), 0, 0, eastl::move(path));
        }

        int w = 0, h = 0, srcCh = 0;

        if (stbi_is_hdr(path.c_str()))
        {
            float* pixels = stbi_loadf(path.c_str(), &w, &h, &srcCh, 4);
            return WrapHdrPixels(pixels, w, h, eastl::move(path));
        }

        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &srcCh, kLdrChannels);
        return WrapLdrPixels(pixels, w, h, eastl::move(path));
    }
}
