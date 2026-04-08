#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <Resource/Asset.h>

namespace Spark::Resource
{
    enum class ImageFormat : uint32_t
    {
        R8,
        RG8,
        RGBA8,
        RGBAF32,    ///< HDR
    };

    class ImageAssetData : public AssetData
    {
    public:
        ImageAssetData(int width, int height, ImageFormat format,
                       eastl::vector<uint8_t> pixels, eastl::string resolvedPath);

        int             GetWidth()    const { return m_width; }
        int             GetHeight()   const { return m_height; }
        ImageFormat     GetFormat()   const { return m_format; }
        bool            IsHDR()       const { return m_format == ImageFormat::RGBAF32; }

        const eastl::vector<uint8_t>& GetPixels() const { return m_pixels; }
        const eastl::string& GetResolvedPath()    const { return m_resolvedPath; }

        uint32_t GetBytesPerPixel() const;

    private:
        int                     m_width{0};
        int                     m_height{0};
        ImageFormat             m_format{ImageFormat::RGBA8};
        eastl::vector<uint8_t>  m_pixels;
        eastl::string           m_resolvedPath;
    };


    class ImageAssetLoader : public AssetLoader
    {
    public:
        ImageAssetLoader();
        ~ImageAssetLoader();

        eastl::unique_ptr<AssetData> Load(const AssetId& id) override;
    };


    class ImageAsset : public Asset
    {
    public:
        static constexpr AssetType GetAssetTypeStatic() { return AssetType::Image; }

        ImageAsset(AssetId id);

        const ImageAssetData* GetImageData() const;

        int         GetWidth()  const;
        int         GetHeight() const;
        ImageFormat GetFormat() const;
    };
}
