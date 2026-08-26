#include "ImageAssetLoader.h"

#include <Resource/AssetBuildContext.h>
#include <VFS/FileSystem.h>

#include <stb_image.h>
#include <nanosvg.h>
#include <nanosvgrast.h>
#include <ktx.h>

#include <Base.h>
#include <Log/ILogSystem.h>

#include "KtxFormatMap.h"

namespace Spark::Resource
{
    namespace
    {
        //! The identity a cache entry carries, against the one the reader expects. A miss
        //! means the 64-bit key collided, so the entry belongs to a different asset.
        bool IdentityMatches(ktxTexture2& tex, eastl::string_view expected)
        {
            ktx_uint32_t length = 0;
            ktx_uint8_t* value  = nullptr;
            if (ktxHashList_FindValue(&tex.kvDataHead, kImageIdentityKey, &length,
                                      reinterpret_cast<void**>(&value)) != KTX_SUCCESS)
            {
                LOG_WARN("[ImageAssetLoader] Cache entry carries no identity.");
                return false;
            }

            // Written with its terminator; compare the text alone.
            const eastl::string_view stored(reinterpret_cast<const char*>(value),
                                            length > 0 ? length - 1 : 0);
            if (stored != expected)
            {
                LOG_WARN("[ImageAssetLoader] Cache entry belongs to another asset: {}",
                    eastl::string(stored.data(), stored.size()).c_str());
                return false;
            }
            return true;
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

    UniquePtr<AssetData> ImageAssetLoader::LoadKtx2(const uint8_t* bytes, size_t size,
                                                    eastl::string_view label,
                                                    eastl::string_view expectedIdentity)
    {
        const eastl::string path(label.data(), label.size());

        ktxTexture2*   tex = nullptr;
        KTX_error_code res = ktxTexture2_CreateFromMemory(
            bytes, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &tex);
        if (res != KTX_SUCCESS)
        {
            LOG_ERROR("[ImageAssetLoader] Failed to open KTX2 {}: {}", path.c_str(), static_cast<int>(res));
            return nullptr;
        }

        // Cheapest rejection first, before any pixels are copied.
        if (!expectedIdentity.empty() && !IdentityMatches(*tex, expectedIdentity))
        {
            ktxTexture_Destroy(ktxTexture(tex));
            return nullptr;
        }

        // Arrays stay out of scope until something produces one; accepting them would mean
        // guessing at a slice order no writer has committed to.
        constexpr uint32_t kNumCubeFaces = 6;
        const bool isCube = tex->numFaces == kNumCubeFaces;
        if (tex->numDimensions != 2 || tex->numLayers != 1 || tex->isArray
            || (tex->numFaces != 1 && !isCube))
        {
            LOG_ERROR("[ImageAssetLoader] {}: only single-layer 2D or cube KTX2 is supported "
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

        const RHI::Format format = FromVkFormat(tex->vkFormat);
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
        result->m_arrayLayers = tex->numFaces;
        result->m_isCubemap   = isCube ? 1 : 0;
        result->m_format      = format;

        // Repacked, not memcpy'd: KTX2 stores levels smallest-first, groups a level's faces
        // together, and may pad between them.
        uint64_t total = 0;
        for (uint32_t level = 0; level < tex->numLevels; ++level)
        {
            total += ktxTexture_GetImageSize(ktxTexture(tex), level) * tex->numFaces;
        }
        result->m_textureBytes.resize(total);
        result->m_mips.reserve(static_cast<size_t>(tex->numFaces) * tex->numLevels);

        uint64_t dstOffset = 0;
        for (uint32_t face = 0; face < tex->numFaces; ++face)
        {
            for (uint32_t level = 0; level < tex->numLevels; ++level)
            {
                ktx_size_t srcOffset = 0;
                res = ktxTexture_GetImageOffset(ktxTexture(tex), level, 0, face, &srcOffset);
                if (res != KTX_SUCCESS)
                {
                    LOG_ERROR("[ImageAssetLoader] {}: GetImageOffset face {} level {} failed: {}",
                              path.c_str(), face, level, static_cast<int>(res));
                    ktxTexture_Destroy(ktxTexture(tex));
                    return nullptr;
                }

                const uint64_t levelBytes = ktxTexture_GetImageSize(ktxTexture(tex), level);
                memcpy(result->m_textureBytes.data() + dstOffset, tex->pData + srcOffset, levelBytes);
                result->m_mips.push_back({dstOffset, levelBytes});
                dstOffset += levelBytes;
            }
        }

        ktxTexture_Destroy(ktxTexture(tex));

        LOG_INFO("[ImageAssetLoader] {}: {}x{}, {} mips, {} slices, cube={}, format={}, {}B "
                 "(already compiled)",
                 path.c_str(), result->m_width, result->m_height, result->m_mipLevels,
                 result->m_arrayLayers, result->IsCubemap(),
                 static_cast<int>(result->m_format), result->m_textureBytes.size());
        return result;
    }

    UniquePtr<AssetData> ImageAssetLoader::LoadEncoded(const AssetId& id,
                                                       const FileSystem& fileSystem)
    {
        eastl::vector<uint8_t> bytes;
        if (!fileSystem.ReadFile(id.GetPath(), bytes))
        {
            return nullptr;
        }
        return MakeUnique<ImageEncodedRawData>(eastl::move(bytes), id.GetPath());
    }

    UniquePtr<AssetData> ImageAssetLoader::LoadSource(const AssetId& id,
                                                      const FileSystem& fileSystem)
    {
        // Through the VFS like every other read, so the path a raw carries stays virtual.
        eastl::vector<uint8_t> bytes;
        if (!fileSystem.ReadFile(id.GetPath(), bytes) || bytes.empty())
        {
            LOG_ERROR("[ImageAssetLoader] Image file not readable: {}", id.GetPath().c_str());
            return nullptr;
        }

        eastl::string path = id.GetPath();

        // nanosvg parses in place and wants a NUL terminator, hence the copy.
        constexpr eastl::string_view kSvgExt = ".svg";
        if (path.size() > kSvgExt.size()
            && path.compare(path.size() - kSvgExt.size(), kSvgExt.size(), kSvgExt.data()) == 0)
        {
            eastl::string svgData(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return DecodeSvg(svgData.c_str(), 0, 0, eastl::move(path));
        }

        return DecodeFromMemory(bytes.data(), bytes.size(), path);
    }
}
