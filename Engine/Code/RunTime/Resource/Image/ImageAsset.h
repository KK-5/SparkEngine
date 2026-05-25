#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <Base.h>
#include <Resource/Asset.h>

#include <RHI/Format.h>

namespace Spark::Resource
{
    enum class ImageFormat : uint8_t
    {
        R8,
        RG8,
        RGBA8,
        RGBAF32,    ///< HDR
    };

    enum class TextureCompression : uint8_t
    {
        None,
        BC1_RGB,
        BC3_RGBA,
        BC4_R,
        BC5_RG,
        BC6H_HDR,
        BC7_RGBA,
    };

    enum class ImageColorSpace : uint8_t
    {
        Linear,
        sRGB,
    };

    class ImageAssetDescriptor : public AssetDescriptor
    {
    public:
        TextureCompression compression  = TextureCompression::BC3_RGBA;
        ImageColorSpace    colorSpace   = ImageColorSpace::sRGB;
        uint32_t           maxMipLevels = 0; // 0 = full chain; 1 = no mips beyond base

        AssetHash Hash() const override;
    };

    struct ImageMipDimensions 
    { 
        uint32_t width; 
        uint32_t height; 
    };

    class ImageAssetRawData : public AssetData
    {
    public:
        ImageAssetRawData(uint32_t width, uint32_t height, ImageFormat format,
                       eastl::vector<uint8_t> pixels, eastl::string resolvedPath);

        uint32_t        GetWidth()    const { return m_width; }
        uint32_t        GetHeight()   const { return m_height; }
        ImageFormat     GetFormat()   const { return m_format; }
        bool            IsHDR()       const { return m_format == ImageFormat::RGBAF32; }

        const eastl::vector<uint8_t>& GetPixels() const { return m_pixels; }
        const eastl::string& GetResolvedPath()    const { return m_resolvedPath; }

        uint32_t GetBytesPerPixel() const;

    private:
        uint32_t                m_width{0};
        uint32_t                m_height{0};
        ImageFormat             m_format{ImageFormat::RGBA8};
        eastl::vector<uint8_t>  m_pixels;
        eastl::string           m_resolvedPath;
    };

    struct ImageMipRange
    {
        uint64_t offset;
        uint64_t size;
    };

    class ImageAssetData : public AssetData
    {
    public:
        ImageAssetData() = default;

        uint32_t          GetWidth()       const { return m_width; }
        uint32_t          GetHeight()      const { return m_height; }
        uint32_t          GetMipLevels()   const { return m_mipLevels; }
        uint32_t          GetArrayLayers() const { return m_arrayLayers; }
        RHI::Format       GetFormat()      const { return m_format; }

        const ImageMipRange& GetMipRange(uint32_t level) const { return m_mips[level]; }
        const eastl::vector<uint8_t>& GetTextureBytes() const { return m_textureBytes; }

        ImageMipDimensions GetMipDimensions(uint32_t level) const 
        {
            return {
                eastl::max(1u, m_width  >> level),
                eastl::max(1u, m_height >> level),
            };
        }

        const uint8_t* GetMipBytes(uint32_t level) const
        {
            return m_textureBytes.data() + m_mips[level].offset;
        }

    private:
        friend class ImageAssetCompiler;
        friend class ImageAssetLoader;

        uint32_t                     m_width{0};
        uint32_t                     m_height{0};
        uint32_t                     m_mipLevels{1};
        uint32_t                     m_arrayLayers{1};
        RHI::Format                  m_format{RHI::Format::R8G8B8A8_UNORM};
        eastl::vector<uint8_t>       m_textureBytes;
        eastl::vector<ImageMipRange> m_mips;
    };

    class ImageAsset : public Asset
    {
    public:
        using Descriptor = ImageAssetDescriptor;

        static constexpr AssetType GetAssetTypeStatic() { return AssetType::Image; }
        static Ptr<AssetDescriptor> DefaultDescriptor();

        explicit ImageAsset(AssetId id);

        const ImageAssetData* GetImageData() const;

        eastl::string_view GetPath() const;

        int         GetWidth()  const;
        int         GetHeight() const;
        uint32_t    GetMipLevels() const;
        RHI::Format GetFormat() const;
    };
}
