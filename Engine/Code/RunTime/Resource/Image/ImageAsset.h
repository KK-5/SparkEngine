#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <Base.h>
#include <Resource/Asset.h>

#include <RHI/Format.h>
#include <RHI/Resource/Image/ImageSubResource.h>

#include "ImageRawTypes.h"

namespace Spark::Resource
{
    class ImageAsset;

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

    enum class ImageUsage : uint8_t
    {
        //! New values MUST be appended: the numeric value feeds ImageAssetDescriptor::Hash
        //! (and thus AssetId identity); reordering would silently invalidate every cached
        //! image sub-asset id.
        Texture2D,          //!< sRGB color 2D (base color, emissive).
        EnvironmentCubemap, //!< equirect source baked into a cube (linear).
        NoColorTexture2D,   //!< non-color data 2D, linear (metallic-roughness, occlusion).
        NormalMap,          //!< tangent-space normal map (always linear).

        //! Derived-only: published already-compiled by the EnvironmentCubemap bake, never
        //! loaded or compiled on their own (ImageAssetBuilder rejects that).
        IrradianceCubemap,  //!< 32^2 single mip; diffuse term.
        PrefilteredCubemap, //!< 128^2, mip N == roughness N/(mips-1); specular term.
    };

    constexpr bool IsCubemapUsage(ImageUsage usage)
    {
        return usage == ImageUsage::EnvironmentCubemap
            || usage == ImageUsage::IrradianceCubemap
            || usage == ImageUsage::PrefilteredCubemap;
    }

    class ImageAssetDescriptor : public AssetDescriptor
    {
    public:
        TextureCompression compression  = TextureCompression::BC3_RGBA;
        ImageColorSpace    colorSpace   = ImageColorSpace::sRGB;
        uint32_t           maxMipLevels = 0; // 0 = full chain; 1 = no mips beyond base

        ImageUsage         usage           = ImageUsage::Texture2D;
        //! Per-face resolution when usage == EnvironmentCubemap. 0 == auto: derived from
        //! the source at compile (equirect H/2, rounded to a power of two, clamped). A
        //! non-zero value is an explicit override.
        uint32_t           cubemapFaceSize = 0;

        AssetHash Hash() const override;
    };

    struct ImageMipDimensions 
    { 
        uint32_t width; 
        uint32_t height; 
    };

    class ImageAssetRawData : public ImageRawData
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
        ~ImageAssetData() override;   // out-of-line: ImageAsset is incomplete here

        //! The IBL cubes from the same environment bake. Null on every other image -- not an
        //! error, but the "no IBL here" signal the lighting path gates on.
        const Ptr<ImageAsset>& GetIrradianceAsset()  const { return m_irradiance; }
        const Ptr<ImageAsset>& GetPrefilteredAsset() const { return m_prefiltered; }

        uint32_t          GetWidth()       const { return m_width; }
        uint32_t          GetHeight()      const { return m_height; }
        uint32_t          GetMipLevels()   const { return m_mipLevels; }
        //! Total slices, cube faces included: 6 for a cube, 6N for a cube array. Same
        //! meaning as RHI::ImageDescriptor::m_arraySize.
        uint32_t          GetArrayLayers() const { return m_arrayLayers; }
        //! Stated, never inferred: a 6-slice 2D array reads identically otherwise.
        bool              IsCubemap()      const { return m_isCubemap != 0; }
        RHI::Format       GetFormat()      const { return m_format; }

        const ImageMipRange& GetSubresourceRange(uint32_t arraySlice, uint32_t mipLevel) const
        {
            return m_mips[RHI::GetImageSubresourceIndex(mipLevel, arraySlice, m_mipLevels)];
        }

        //! Slice 0's chain -- the whole payload of a plain 2D image.
        const ImageMipRange& GetMipRange(uint32_t level) const { return GetSubresourceRange(0, level); }

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
            return m_textureBytes.data() + GetMipRange(level).offset;
        }

    private:
        friend class ImageAssetCompiler;
        friend class ImageAssetLoader;
        friend class ImageAssetBuilder;

        uint32_t                     m_width{0};
        uint32_t                     m_height{0};
        uint32_t                     m_mipLevels{1};
        uint32_t                     m_arrayLayers{1};
        uint32_t                     m_isCubemap{0};
        RHI::Format                  m_format{RHI::Format::R8G8B8A8_UNORM};
        eastl::vector<uint8_t>       m_textureBytes;

        //! Slice-major, mip-inner: m_arrayLayers * m_mipLevels entries.
        eastl::vector<ImageMipRange> m_mips;

        // Strong refs (not AssetIds) so the children cannot outlive-fail to resolve: a
        // released id would read as "no IBL products". No cycle -- children never point back.
        Ptr<ImageAsset>              m_irradiance;
        Ptr<ImageAsset>              m_prefiltered;
    };

    class ImageAsset : public Asset
    {
    public:
        using Descriptor = ImageAssetDescriptor;

        static constexpr AssetType GetAssetTypeStatic() { return AssetType::Image; }
        static Ptr<AssetDescriptor> DefaultDescriptor();

        //! Descriptor for HDR (equirectangular) sources: Linear + uncompressed, and
        //! usage == EnvironmentCubemap so registration/compile bakes them into a cube.
        //! Sibling of DefaultDescriptor; a distinct identity from the same file loaded 2D.
        static Ptr<AssetDescriptor> DefaultHDRDescriptor();

        //! Canonical descriptor for a given usage — the single place that pins colorSpace
        //! (and compression / bake path) per usage. Callers outside the model pipeline
        //! (e.g. explicit texture registration) request an image by usage through this.
        //! Returns a shared per-usage singleton; DefaultDescriptor/DefaultHDRDescriptor
        //! are thin aliases over Texture2D / EnvironmentCubemap.
        static Ptr<AssetDescriptor> DescriptorForUsage(ImageUsage usage);

        //! The single place image sub-asset ids are built -- glTF's embedded images and an
        //! environment bake's IBL products, separated only by subLabel namespace.
        static AssetId MakeSubId(const AssetId& parentId, eastl::string_view subLabel, ImageUsage usage);

        static uint32_t ComputeMipLevels(uint32_t width, uint32_t height, uint32_t maxLevel);

        explicit ImageAsset(AssetId id);

        const ImageAssetData* GetImageData() const;

        //! Null when there is no data yet, or no IBL products. See ImageAssetData.
        Ptr<ImageAsset> GetIrradianceAsset()  const;
        Ptr<ImageAsset> GetPrefilteredAsset() const;

        eastl::string_view GetPath() const;

        int         GetWidth()  const;
        int         GetHeight() const;
        uint32_t    GetMipLevels() const;
        RHI::Format GetFormat() const;
    };
}
