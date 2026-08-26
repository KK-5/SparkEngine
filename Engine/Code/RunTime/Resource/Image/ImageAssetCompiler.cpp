#include "ImageAssetCompiler.h"

#include <stb_image_resize2.h>
#include <stb_dxt.h>
#include <ktx.h>

#include "EnvironmentBaker.h"
#include "ImageAssetLoader.h"   // kImageIdentityKey
#include "KtxFormatMap.h"

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

    }

    eastl::vector<uint8_t> ImageAssetCompiler::SerializeToKtx2(const ImageAssetData& data,
                                                               eastl::string_view identity)
    {
        // KTX2 splits into numFaces x numLayers what the asset keeps as one slice count.
        // That split lives here and nowhere else.
        constexpr uint32_t kNumCubeFaces = 6;
        const uint32_t numFaces  = data.IsCubemap() ? kNumCubeFaces : 1;
        const uint32_t numLayers = data.GetArrayLayers() / numFaces;

        if (numLayers == 0 || numLayers * numFaces != data.GetArrayLayers())
        {
            LOG_ERROR("[ImageAssetCompiler] A cube must have a multiple of {} slices, got {}",
                kNumCubeFaces, data.GetArrayLayers());
            return {};
        }

        ktxTextureCreateInfo info{};
        info.vkFormat        = ToVkFormat(data.GetFormat());
        info.baseWidth       = data.GetWidth();
        info.baseHeight      = data.GetHeight();
        info.baseDepth       = 1;
        info.numDimensions   = 2;
        info.numLevels       = data.GetMipLevels();
        info.numLayers       = numLayers;
        info.numFaces        = numFaces;
        info.isArray         = numLayers > 1 ? KTX_TRUE : KTX_FALSE;
        info.generateMipmaps = KTX_FALSE;

        if (info.vkFormat == kVkFormatUndefined)
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

        // slice = layer * 6 + face, D3D12's subresource order.
        for (uint32_t layer = 0; layer < numLayers; ++layer)
        {
            for (uint32_t face = 0; face < numFaces; ++face)
            {
                const uint32_t slice = layer * numFaces + face;
                for (uint32_t level = 0; level < data.GetMipLevels(); ++level)
                {
                    const ImageMipRange& sub = data.GetSubresourceRange(slice, level);
                    res = ktxTexture_SetImageFromMemory(
                        ktxTexture(tex),
                        level, layer, face,
                        data.m_textureBytes.data() + sub.offset,
                        static_cast<ktx_size_t>(sub.size));
                    if (res != KTX_SUCCESS)
                    {
                        LOG_ERROR("[ImageAssetCompiler] SetImageFromMemory slice {} level {} "
                                  "failed: {}", slice, level, static_cast<int>(res));
                        ktxTexture_Destroy(ktxTexture(tex));
                        return {};
                    }
                }
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
        auto& raw = static_cast<ImageRawData&>(rawData);
        switch (raw.GetKind())
        {
            case ImageRawData::Kind::Baked:
            {
                return AssembleCubemapData(
                    eastl::move(static_cast<ImageBakedRawData&>(raw).GetCube()));
            }
            case ImageRawData::Kind::Encoded:
            {
                const auto& encoded = static_cast<ImageEncodedRawData&>(raw);
                return ImageAssetLoader::LoadKtx2(encoded.GetBytes().data(),
                                                  encoded.GetBytes().size(),
                                                  encoded.GetResolvedPath(),
                                                  /*expectedIdentity=*/ {});
            }
            case ImageRawData::Kind::Pixels:
                break;
        }
        return CompilePixels(id, static_cast<ImageAssetRawData&>(raw));
    }

    BakedEnvironment ImageAssetCompiler::BakeEnvironment(const AssetId& id,
                                                          const ImageAssetRawData& equirect,
                                                          const ImageAssetDescriptor& desc)
    {
        if (!m_baker.IsInitialized())
        {
            LOG_ERROR("[ImageAssetCompiler] EnvironmentCubemap asset requested but the baker "
                      "is not initialized (call InitEnvironmentBaker during setup): {}",
                      id.GetPath().c_str());
            return {};
        }

        // cubemapFaceSize == 0 means auto: size the cube from the decoded source.
        const uint32_t faceSize = desc.cubemapFaceSize != 0
            ? desc.cubemapFaceSize
            : EnvironmentBaker::RecommendedFaceSize(equirect.GetHeight());

        return m_baker.Bake(equirect, faceSize);
    }

    UniquePtr<AssetData> ImageAssetCompiler::CompilePixels(const AssetId& id, ImageAssetRawData& raw)
    {
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
        result->m_isCubemap   = 0;
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

        constexpr uint32_t kNumCubeFaces = 6;

        auto result = MakeUnique<ImageAssetData>();
        result->m_width       = baked.faceSize;
        result->m_height      = baked.faceSize;
        result->m_mipLevels   = baked.mipLevels;
        result->m_arrayLayers = kNumCubeFaces;
        result->m_isCubemap   = 1;
        result->m_format      = baked.format; // R16G16B16A16_FLOAT
        result->m_textureBytes = eastl::move(baked.faceBytes);

        // Face-major, mip-inner: how the baker packs its readback, and how
        // GetImageSubresourceIndex numbers slices, so the table fills straight through.
        // Extents come from the same GetImageSubresourceLayout the upload path uses.
        result->m_mips.reserve(static_cast<size_t>(kNumCubeFaces) * baked.mipLevels);
        uint64_t offset = 0;
        for (uint32_t face = 0; face < kNumCubeFaces; ++face)
        {
            for (uint32_t mip = 0; mip < baked.mipLevels; ++mip)
            {
                const uint32_t extent = eastl::max(1u, baked.faceSize >> mip);
                const uint64_t bytes = RHI::GetImageSubresourceLayout(
                    RHI::Size(extent, extent, 1), baked.format).m_bytesPerImage;
                result->m_mips.push_back({offset, bytes});
                offset += bytes;
            }
        }

        if (offset != result->m_textureBytes.size())
        {
            LOG_ERROR("[ImageAssetCompiler] AssembleCubemapData: baked payload is {}B but its "
                      "{} faces x {} mips describe {}B.",
                result->m_textureBytes.size(), kNumCubeFaces, baked.mipLevels, offset);
            return nullptr;
        }

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