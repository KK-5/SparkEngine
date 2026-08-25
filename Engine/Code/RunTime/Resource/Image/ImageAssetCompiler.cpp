#include "ImageAssetCompiler.h"

#include <stb_image_resize2.h>
#include <stb_dxt.h>
#include <ktx.h>

#include "EnvironmentBaker.h"
#include "ImageAssetLoader.h"   // kImageIdentityKey

namespace Spark::Resource
{
    namespace
    {
        stbir_pixel_layout PickPixelLayout(ImageFormat format)
        {
            switch (format)
            {
                case ImageFormat::R8:      return STBIR_1CHANNEL;
                case ImageFormat::RG8:     return STBIR_2CHANNEL;
                case ImageFormat::RGBA8:   return STBIR_RGBA;
                case ImageFormat::RGBAF32: return STBIR_RGBA;
            }
            return STBIR_RGBA;
        }

        void ResizeMip(
            const uint8_t*  src, 
            uint32_t        srcW, 
            uint32_t        srcH,
            uint8_t*        dst, 
            uint32_t        dstW, 
            uint32_t        dstH,
            ImageFormat     format, 
            ImageColorSpace colorSpace
        )
        {
            const stbir_pixel_layout layout = PickPixelLayout(format);

            if (format == ImageFormat::RGBAF32)
            {
                // HDR 永远 linear 空间
                stbir_resize_float_linear(
                    reinterpret_cast<const float*>(src),
                    static_cast<int>(srcW), static_cast<int>(srcH), 0,
                    reinterpret_cast<float*>(dst),
                    static_cast<int>(dstW), static_cast<int>(dstH), 0,
                    layout);
            }
            else if (colorSpace == ImageColorSpace::sRGB && format == ImageFormat::RGBA8)
            {
                stbir_resize_uint8_srgb(
                    src, static_cast<int>(srcW), static_cast<int>(srcH), 0,
                    dst, static_cast<int>(dstW), static_cast<int>(dstH), 0,
                    layout);
            }
            else
            {
                stbir_resize_uint8_linear(
                    src, static_cast<int>(srcW), static_cast<int>(srcH), 0,
                    dst, static_cast<int>(dstW), static_cast<int>(dstH), 0,
                    layout);
            }
        }

        uint32_t BlockBytes(TextureCompression compression)
        {
            switch (compression)
            {
                case TextureCompression::BC1_RGB:  return 8;
                case TextureCompression::BC3_RGBA: return 16;
                case TextureCompression::BC4_R:    return 8;
                case TextureCompression::BC5_RG:   return 16;
                case TextureCompression::BC6H_HDR: return 16;
                case TextureCompression::BC7_RGBA: return 16;
                case TextureCompression::None:     return 0;
            }
            return 0;
        }

        TextureCompression ResolveCompression(ImageFormat format, TextureCompression requested)
        {
            switch (requested)
            {
                case TextureCompression::None:
                    return requested;
                case TextureCompression::BC1_RGB:
                    return format == ImageFormat::RGBA8 ? requested : TextureCompression::None;
                case TextureCompression::BC3_RGBA:
                    return format == ImageFormat::RGBA8 ? requested : TextureCompression::None;
                case TextureCompression::BC4_R:    
                    return format == ImageFormat::R8 ? requested : TextureCompression::None;
                case TextureCompression::BC5_RG:   
                    return format == ImageFormat::RG8 ? requested : TextureCompression::None;
                case TextureCompression::BC7_RGBA:
                    return TextureCompression::None;
                case TextureCompression::BC6H_HDR:
                    return TextureCompression::None;
                default:
                    return TextureCompression::None;
            }
        }

        void GatherBlockRGBA(const uint8_t* src, uint32_t width, uint32_t height,
                             uint32_t blockX, uint32_t blockY, uint8_t dst[64])
        {
            for (uint32_t py = 0; py < 4; ++py)
            {
                for (uint32_t px = 0; px < 4; ++px)
                {
                    const uint32_t sx = eastl::min(blockX * 4 + px, width - 1);
                    const uint32_t sy = eastl::min(blockY * 4 + py, height - 1);
                    const uint8_t* p = src + (sy * width + sx) * 4;
                    uint8_t* d = dst + (py * 4 + px) * 4;
                    d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; d[3] = p[3];
                }
            }
        }

        // BC4 一通道版
        void GatherBlockR(const uint8_t* src, uint32_t width, uint32_t height,
                          uint32_t blockX, uint32_t blockY, uint8_t dst[16])
        {
            for (int py = 0; py < 4; ++py)
            {
                for (int px = 0; px < 4; ++px)
                {
                    const uint32_t sx = eastl::min(blockX * 4 + px, width - 1);
                    const uint32_t sy = eastl::min(blockY * 4 + py, height - 1);
                    dst[py * 4 + px] = src[sy * width + sx];
                }
            }
        }

        // BC5 两通道版
        void GatherBlockRG(const uint8_t* src, uint32_t w, uint32_t h,
                           uint32_t bx, uint32_t by, uint8_t dst[32])
        {
            for (uint32_t py = 0; py < 4; ++py)
            {
                for (uint32_t px = 0; px < 4; ++px)
                {
                    const uint32_t sx = eastl::min(bx * 4 + px, w - 1);
                    const uint32_t sy = eastl::min(by * 4 + py, h - 1);
                    const uint8_t* p = src + (sy * w + sx) * 2;
                    uint8_t* d = dst + (py * 4 + px) * 2;
                    d[0] = p[0]; d[1] = p[1];
                }
            }
        }

        void EncodeBCnMip(const uint8_t* src, uint32_t width, uint32_t height,
                    uint8_t* dst, TextureCompression compression)
        {
            const uint32_t blocksW = (width + 3) / 4;
            const uint32_t blocksH = (height + 3) / 4;
            const uint32_t blockSize = BlockBytes(compression);

            for (uint32_t by = 0; by < blocksH; ++by)
            {
                for (uint32_t bx = 0; bx < blocksW; ++bx)
                {
                    uint8_t* dstBlock = dst + (by * blocksW + bx) * blockSize;
                    switch (compression)
                    {
                        case TextureCompression::BC1_RGB:
                        {
                            uint8_t block[64];
                            GatherBlockRGBA(src, width, height, bx, by, block);
                            stb_compress_dxt_block(dstBlock, block, /*alpha=*/ 0, STB_DXT_NORMAL);
                            break;
                        }
                        case TextureCompression::BC3_RGBA:
                        {
                            uint8_t block[64];
                            GatherBlockRGBA(src, width, height, bx, by, block);
                            stb_compress_dxt_block(dstBlock, block, /*alpha=*/ 1, STB_DXT_NORMAL);
                            break;
                        }
                        case TextureCompression::BC4_R:
                        {
                            uint8_t block[16];
                            GatherBlockR(src, width, height, bx, by, block);
                            stb_compress_bc4_block(dstBlock, block);
                            break;
                        }
                        case TextureCompression::BC5_RG:
                        {
                            uint8_t block[32];
                            GatherBlockRG(src, width, height, bx, by, block);
                            stb_compress_bc5_block(dstBlock, block);
                            break;
                        }
                        default:
                            ASSERT(false, "EncodeBCnMip: unsupported compression");
                            return;
                    }

                }
            }
        }

        uint32_t MapToVkFormat(RHI::Format format)
        {
            switch (format)
            {
                // 未压缩
                case RHI::Format::R8_UNORM:              return VkFormatValue::R8_UNORM;
                case RHI::Format::R8G8_UNORM:            return VkFormatValue::R8G8_UNORM;
                case RHI::Format::R8G8B8A8_UNORM:        return VkFormatValue::R8G8B8A8_UNORM;
                case RHI::Format::R8G8B8A8_UNORM_SRGB:   return VkFormatValue::R8G8B8A8_SRGB;
                case RHI::Format::R32G32B32A32_FLOAT:    return VkFormatValue::R32G32B32A32_SFLOAT;

                // BC1 —— DX12 的 BC1_UNORM 等价 Vulkan 的 BC1_RGBA_UNORM_BLOCK
                // （DXGI 不区分 RGB/RGBA，BC1 总是 4 通道带 1-bit alpha）
                case RHI::Format::BC1_UNORM:             return VkFormatValue::BC1_RGBA_UNORM_BLOCK;
                case RHI::Format::BC1_UNORM_SRGB:        return VkFormatValue::BC1_RGBA_SRGB_BLOCK;

                case RHI::Format::BC3_UNORM:             return VkFormatValue::BC3_UNORM_BLOCK;
                case RHI::Format::BC3_UNORM_SRGB:        return VkFormatValue::BC3_SRGB_BLOCK;

                case RHI::Format::BC4_UNORM:             return VkFormatValue::BC4_UNORM_BLOCK;
                case RHI::Format::BC4_SNORM:             return VkFormatValue::BC4_SNORM_BLOCK;

                case RHI::Format::BC5_UNORM:             return VkFormatValue::BC5_UNORM_BLOCK;
                case RHI::Format::BC5_SNORM:             return VkFormatValue::BC5_SNORM_BLOCK;

                case RHI::Format::BC6H_UF16:             return VkFormatValue::BC6H_UFLOAT_BLOCK;
                case RHI::Format::BC6H_SF16:             return VkFormatValue::BC6H_SFLOAT_BLOCK;

                case RHI::Format::BC7_UNORM:             return VkFormatValue::BC7_UNORM_BLOCK;
                case RHI::Format::BC7_UNORM_SRGB:        return VkFormatValue::BC7_SRGB_BLOCK;

                default:
                    return VkFormatValue::UNDEFINED;
            }
        }
    }

    eastl::vector<uint8_t> ImageAssetCompiler::SerializeToKtx2(const ImageAssetData& data,
                                                               eastl::string_view identity)
    {
        ktxTextureCreateInfo info{};
        info.vkFormat        = MapToVkFormat(data.GetFormat());
        info.baseWidth       = data.GetWidth();
        info.baseHeight      = data.GetHeight();
        info.baseDepth       = 1;
        info.numDimensions   = 2;
        info.numLevels       = data.GetMipLevels();
        info.numLayers       = data.GetArrayLayers();
        info.numFaces        = 1;
        info.generateMipmaps = KTX_FALSE;

        if (info.vkFormat == VkFormatValue::UNDEFINED)
        {
            LOG_ERROR("[ImageAssetCompiler] No VkFormat mapping for RHI::Format {}",
                static_cast<int>(data.GetFormat()));
            return {};
        }

        ktxTexture2* tex = nullptr;
        KTX_error_code res = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &tex);
        if (res != KTX_SUCCESS)
        {
            LOG_ERROR("[ImageAssetCompiler] ktxTexture2_Create failed: {}", static_cast<int>(res));
            return {};
        }

        for (uint32_t level = 0; level < data.GetMipLevels(); ++level)
        {
            const ImageMipRange& mip = data.GetMipRange(level);
            const uint8_t* src = data.m_textureBytes.data() + mip.offset;
            res = ktxTexture_SetImageFromMemory(
                ktxTexture(tex),
                level, /*layer=*/ 0, /*face=*/ 0,
                src,
                static_cast<ktx_size_t>(mip.size));
            if (res != KTX_SUCCESS)
            {
                LOG_ERROR("[ImageAssetCompiler] SetImageFromMemory level {} failed: {}",
                    level, static_cast<int>(res));
                ktxTexture_Destroy(ktxTexture(tex));
                return {};
            }
        }

        if (!identity.empty())
        {
            // Copied so the value is NUL-terminated: `identity` is a view and the length
            // passed below counts that terminator, so writing straight from it would read
            // one byte past the end.
            const eastl::string value(identity.data(), identity.size());
            res = ktxHashList_AddKVPair(&tex->kvDataHead, kImageIdentityKey,
                static_cast<ktx_uint32_t>(value.size() + 1), value.c_str());
            if (res != KTX_SUCCESS)
            {
                LOG_ERROR("[ImageAssetCompiler] AddKVPair failed: {}", static_cast<int>(res));
                ktxTexture_Destroy(ktxTexture(tex));
                return {};
            }
        }

        ktx_uint8_t* outBytes = nullptr;
        ktx_size_t   outSize  = 0;
        res = ktxTexture_WriteToMemory(ktxTexture(tex), &outBytes, &outSize);
        if (res != KTX_SUCCESS)
        {
            LOG_ERROR("[ImageAssetCompiler] WriteToMemory failed: {}", static_cast<int>(res));
            ktxTexture_Destroy(ktxTexture(tex));
            return {};
        }

        eastl::vector<uint8_t> blob(outBytes, outBytes + outSize);
        free(outBytes);  // libktx 用 malloc 分配，必须 free
        ktxTexture_Destroy(ktxTexture(tex));
        return blob;
    }

    UniquePtr<AssetData> ImageAssetCompiler::Compile(const AssetId& id, AssetData& rawData)
    {
        auto& raw = static_cast<ImageAssetRawData&>(rawData);

        const auto* descPtr = static_cast<const ImageAssetDescriptor*>(id.GetDescriptor());
        const ImageAssetDescriptor fallback{};
        const ImageAssetDescriptor& desc = descPtr ? *descPtr : fallback;

        // A normal map is tangent-space vector data — it must never be sRGB-decoded, or the
        // vectors get gamma-distorted. DescriptorForUsage(NormalMap) already pins Linear;
        // this catches any caller that hand-builds a NormalMap descriptor with sRGB.
        ASSERT(desc.usage != ImageUsage::NormalMap || desc.colorSpace == ImageColorSpace::Linear,
            "[ImageAssetCompiler] NormalMap image must be Linear, not sRGB");

        const uint32_t srcW = static_cast<uint32_t>(raw.GetWidth());
        const uint32_t srcH = static_cast<uint32_t>(raw.GetHeight());
        const ImageFormat srcFormat = raw.GetFormat();
        const uint32_t bytePerPixel = raw.GetBytesPerPixel();

        TextureCompression compression = ResolveCompression(srcFormat, desc.compression);
        if (compression != desc.compression)
        {
            LOG_WARN("[ImageAssetCompiler] Compression {} not supported for ImageFormat {}; "
                "falling back to {}",
                static_cast<int>(desc.compression), static_cast<int>(srcFormat), static_cast<int>(compression));
        }
        // Grenerate mip buffer
        const uint32_t mipLevels = ImageAsset::ComputeMipLevels(srcW, srcH, desc.maxMipLevels);

        eastl::vector<uint8_t> uncompressed;
        eastl::vector<ImageMipRange> mips;
        mips.reserve(mipLevels);
        uint64_t totalBytes = 0;
        uint32_t mipW = srcW, mipH = srcH;
        for (uint32_t level = 0; level < mipLevels; ++level)
        {
            const uint64_t mipBytes = static_cast<uint64_t>(mipW) * mipH * bytePerPixel;
            mips.push_back({totalBytes, mipBytes});
            totalBytes += mipBytes;
            mipW = eastl::max(1u, mipW / 2);
            mipH = eastl::max(1u, mipH / 2);
        }
        uncompressed.resize(totalBytes);

        const auto& srcPixels = raw.GetPixels();
        ASSERT(srcPixels.size() == mips[0].size,
            "[ImageAssetCompiler] Raw pixel size mismatch: expected {}, got {}",
            mips[0].size, srcPixels.size());
        memcpy(uncompressed.data(), srcPixels.data(), mips[0].size);

        uint32_t prevW = srcW, prevH = srcH;
        for (uint32_t level = 1; level < mipLevels; ++level)
        {
            const uint32_t curW = eastl::max(1u, prevW / 2);
            const uint32_t curH = eastl::max(1u, prevH / 2);

            const uint8_t* src = uncompressed.data() + mips[level - 1].offset;
            uint8_t*       dst = uncompressed.data() + mips[level].offset;

            ResizeMip(src, prevW, prevH, dst, curW, curH, srcFormat, desc.colorSpace);

            prevW = curW;
            prevH = curH;
        }

        auto result = MakeUnique<ImageAssetData>();

        if (compression == TextureCompression::None)
        {
            result->m_textureBytes = eastl::move(uncompressed);
            result->m_mips         = eastl::move(mips);
        }
        else
        {
            const uint32_t blockBytes = BlockBytes(compression);
            eastl::vector<ImageMipRange> bcMips;
            bcMips.reserve(mipLevels);

            uint64_t total = 0;
            uint32_t bcW = srcW, bcH = srcH;
            for (uint32_t level = 0; level < mipLevels; ++level)
            {
                const uint32_t bw = (bcW + 3) / 4;
                const uint32_t bh = (bcH + 3) / 4;
                const uint64_t mb = static_cast<uint64_t>(bw) * bh * blockBytes;
                bcMips.push_back({total, mb});
                total += mb;
                bcW = eastl::max(1u, bcW / 2);
                bcH = eastl::max(1u, bcH / 2);
            }

            result->m_textureBytes.resize(total);

            bcW = srcW; bcH  = srcH;
            for (uint32_t level = 0; level < mipLevels; ++level)
            {
                EncodeBCnMip(uncompressed.data() + mips[level].offset, bcW, bcH,
                            result->m_textureBytes.data() + bcMips[level].offset,
                            compression);
                bcW = eastl::max(1u, bcW / 2);
                bcH = eastl::max(1u, bcH / 2);
            }
            result->m_mips = eastl::move(bcMips);
        }

        result->m_width       = srcW;
        result->m_height      = srcH;
        result->m_mipLevels   = mipLevels;
        result->m_arrayLayers = 1;
        result->m_format      = MapToRHIFormat(srcFormat, compression, desc.colorSpace);

        LOG_INFO("[ImageAssetCompiler] {}: {}x{}, {} mips, usage={} colorSpace={} format={}, payload {}B",
            raw.GetResolvedPath().c_str(), srcW, srcH, mipLevels,
            static_cast<int>(desc.usage), static_cast<int>(desc.colorSpace),
            static_cast<int>(result->m_format),
            result->m_textureBytes.size());

        return result;
    }


    UniquePtr<AssetData> ImageAssetCompiler::AssembleCubemapData(BakedCubemap&& baked)
    {
        if (!baked.IsValid())
        {
            LOG_ERROR("[ImageAssetCompiler] AssembleCubemapData: invalid baked cubemap.");
            return nullptr;
        }

        // Tight rows, per-face bytes of the base mip. Block-compressed formats would need
        // a per-block computation; every bake product is uncompressed today.
        constexpr uint32_t kNumCubeFaces = 6;
        const uint32_t bytesPerPixel = RHI::GetFormatSize(baked.format);
        const uint64_t perFaceBytes =
            static_cast<uint64_t>(baked.faceSize) * baked.faceSize * bytesPerPixel;

        auto result = MakeUnique<ImageAssetData>();
        result->m_width       = baked.faceSize;
        result->m_height      = baked.faceSize;
        result->m_mipLevels   = baked.mipLevels;
        result->m_arrayLayers = kNumCubeFaces;
        result->m_format      = baked.format; // R16G16B16A16_FLOAT
        result->m_textureBytes = eastl::move(baked.faceBytes);

        // m_textureBytes is FACE-MAJOR, MIP-INNER ([f0m0][f0m1]...[f1m0]...), matching the
        // order AsyncUploadSystem walks subresources in. That order is the only layout
        // description the upload path needs -- it recomputes each subresource's tight
        // extent via GetImageSubresourceLayout and never reads an offset table.
        //
        // m_mips, by contrast, is indexed by mip level ALONE (no layer dimension), so it
        // cannot describe this buffer. Its only consumers are GetMipRange and
        // SerializeToKtx2, both of which serve disk caching -- deliberately deferred to
        // the engine-wide asset cache work, so a base-mip placeholder stays sufficient.
        // Do not derive upload offsets from it.
        result->m_mips.push_back({0, perFaceBytes});

        LOG_INFO("[ImageAssetCompiler] baked cubemap: {} faces @ {}px, {} mips, {}B",
            kNumCubeFaces, baked.faceSize, baked.mipLevels, result->m_textureBytes.size());

        return result;
    }

    RHI::Format ImageAssetCompiler::MapToRHIFormat(ImageFormat src, TextureCompression compression, ImageColorSpace colorSpace)
    {
        const bool srgb = (colorSpace == ImageColorSpace::sRGB);
        switch (compression)
        {
            case TextureCompression::None:
            {
                switch (src)
                {
                    case ImageFormat::R8:      return RHI::Format::R8_UNORM;
                    case ImageFormat::RG8:     return RHI::Format::R8G8_UNORM;
                    case ImageFormat::RGBA8:
                        return srgb ? RHI::Format::R8G8B8A8_UNORM_SRGB
                                    : RHI::Format::R8G8B8A8_UNORM;
                    case ImageFormat::RGBAF32:
                        return RHI::Format::R32G32B32A32_FLOAT;
                }
                return RHI::Format::R8G8B8A8_UNORM;
            }
            case TextureCompression::BC1_RGB:  return srgb ? RHI::Format::BC1_UNORM_SRGB : RHI::Format::BC1_UNORM;
            case TextureCompression::BC3_RGBA: return srgb ? RHI::Format::BC3_UNORM_SRGB : RHI::Format::BC3_UNORM;
            case TextureCompression::BC4_R:    return RHI::Format::BC4_UNORM;     // sRGB 对单通道无意义
            case TextureCompression::BC5_RG:   return RHI::Format::BC5_UNORM;     // 同上
            case TextureCompression::BC6H_HDR: return RHI::Format::BC6H_UF16;     // unsigned, Step 9 fallback
            case TextureCompression::BC7_RGBA: return srgb ? RHI::Format::BC7_UNORM_SRGB : RHI::Format::BC7_UNORM;
        }

        return RHI::Format::R8G8B8A8_UNORM;
    }
}