#include "ImageAssetCompiler.h"

namespace Spark::Resource
{
    UniquePtr<AssetData> ImageAssetCompiler::Compile(const AssetId& id, AssetData& rawData)
    {
        auto* raw = static_cast<ImageAssetRawData*>(&rawData);

        const ImageCompileDescriptor desc = GetDescriptor(id);

        auto result = MakeUnique<ImageAssetData>();
        result->m_width       = static_cast<uint32_t>(raw->GetWidth());
        result->m_height      = static_cast<uint32_t>(raw->GetHeight());
        result->m_mipLevels   = 1;
        result->m_arrayLayers = 1;
        result->m_format      = MapToRHIFormat(raw->GetFormat(), desc.colorSpace);

        // Passthrough: 直接拷贝 raw pixels。Step 3 起会生成 mip + 压缩
        result->m_textureBytes = raw->GetPixels();
        result->m_mips.push_back({0, result->m_textureBytes.size()});

        return result;
    }

    void ImageAssetCompiler::SetCompileDescriptor(const AssetId& id, const ImageCompileDescriptor& desc)
    {
        m_descriptors[id] = desc;
    }

    ImageCompileDescriptor ImageAssetCompiler::GetDescriptor(const AssetId& id) const
    {
        auto it = m_descriptors.find(id);
        if (it != m_descriptors.end())
        {
            return it->second;
        }
        return ImageCompileDescriptor{};  // 默认 BC3_RGBA + sRGB
    }

    RHI::Format ImageAssetCompiler::MapToRHIFormat(ImageFormat src, ImageColorSpace cs)
    {
        const bool srgb = (cs == ImageColorSpace::sRGB);
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
}