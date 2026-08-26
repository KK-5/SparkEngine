#include "ImageAsset.h"

#include <EASTLEX/hash.h>
#include <HashString/HashString.h>
#include <Log/ILogSystem.h>

#include "EnvironmentBaker.h"

namespace Spark::Resource
{
    // ---- ImageAssetDescriptor ----

    AssetHash ImageAssetDescriptor::Hash() const
    {
        size_t h = static_cast<size_t>(HashString("ImageAssetDescriptor").value());
        eastl::hash_combine(h, static_cast<size_t>(compression));
        eastl::hash_combine(h, static_cast<size_t>(colorSpace));
        eastl::hash_combine(h, static_cast<size_t>(maxMipLevels));
        eastl::hash_combine(h, static_cast<size_t>(usage));
        // faceSize only matters for the cubemap path; folding it unconditionally
        // would make two otherwise-identical Texture2D descriptors hash apart.
        if (IsCubemapUsage(usage))
        {
            eastl::hash_combine(h, static_cast<size_t>(cubemapFaceSize));
        }
        return static_cast<AssetHash>(h);
    }

    // ---- ImageAssetData ----

    ImageAssetData::~ImageAssetData() = default;

    ImageAssetRawData::ImageAssetRawData(uint32_t width, uint32_t height, ImageFormat format,
                                   eastl::vector<uint8_t> pixels, eastl::string resolvedPath)
        : ImageRawData(Kind::Pixels)
        , m_width(width)
        , m_height(height)
        , m_format(format)
        , m_pixels(eastl::move(pixels))
        , m_resolvedPath(eastl::move(resolvedPath))
    {}

    uint32_t ImageAssetRawData::GetBytesPerPixel() const
    {
        switch (m_format)
        {
        case ImageFormat::R8:      return 1;
        case ImageFormat::RG8:     return 2;
        case ImageFormat::RGBA8:   return 4;
        case ImageFormat::RGBAF32: return 16;
        default:                   return 4;
        }
    }

    // ---- ImageAsset ----

    Ptr<AssetDescriptor> ImageAsset::DescriptorForUsage(ImageUsage usage)
    {
        switch (usage)
        {
        case ImageUsage::Texture2D:
        {
            // sRGB color 2D. Kept field-identical to the historical default descriptor so
            // existing image sub-asset AssetIds (and their caches) do not shift.
            static Ptr<AssetDescriptor> instance(new ImageAssetDescriptor{});
            return instance;
        }
        case ImageUsage::NoColorTexture2D:
        {
            static Ptr<AssetDescriptor> instance = []
            {
                auto* desc = new ImageAssetDescriptor{};
                desc->usage      = ImageUsage::NoColorTexture2D;
                desc->colorSpace = ImageColorSpace::Linear;
                return Ptr<AssetDescriptor>(desc);
            }();
            return instance;
        }
        case ImageUsage::NormalMap:
        {
            // Always linear; BC5 two-channel + z-reconstruct is a later optimization.
            static Ptr<AssetDescriptor> instance = []
            {
                auto* desc = new ImageAssetDescriptor{};
                desc->usage      = ImageUsage::NormalMap;
                desc->colorSpace = ImageColorSpace::Linear;
                return Ptr<AssetDescriptor>(desc);
            }();
            return instance;
        }
        case ImageUsage::EnvironmentCubemap:
        {
            static Ptr<AssetDescriptor> instance = []
            {
                auto* desc = new ImageAssetDescriptor{};
                desc->usage           = ImageUsage::EnvironmentCubemap;
                desc->colorSpace      = ImageColorSpace::Linear;
                desc->compression     = TextureCompression::None;
                desc->cubemapFaceSize = 0; // auto: derived from the source at compile
                return Ptr<AssetDescriptor>(desc);
            }();
            return instance;
        }
        case ImageUsage::IrradianceCubemap:
        {
            // Nothing loads or compiles through these; the descriptor only records what the
            // product is. faceSize is the baker's, so the id shifts if the bake shape does.
            static Ptr<AssetDescriptor> instance = []
            {
                auto* desc = new ImageAssetDescriptor{};
                desc->usage           = ImageUsage::IrradianceCubemap;
                desc->colorSpace      = ImageColorSpace::Linear;
                desc->compression     = TextureCompression::None;
                desc->cubemapFaceSize = EnvironmentBaker::kIrradianceSize;
                return Ptr<AssetDescriptor>(desc);
            }();
            return instance;
        }
        case ImageUsage::PrefilteredCubemap:
        {
            static Ptr<AssetDescriptor> instance = []
            {
                auto* desc = new ImageAssetDescriptor{};
                desc->usage           = ImageUsage::PrefilteredCubemap;
                desc->colorSpace      = ImageColorSpace::Linear;
                desc->compression     = TextureCompression::None;
                // 0 == derived at bake, same convention as EnvironmentCubemap: the real
                // size is capped by the sky cube (EnvironmentBaker::PrefilterFaceSize), so
                // no constant here could be true for every source.
                desc->cubemapFaceSize = 0;
                desc->maxMipLevels    = EnvironmentBaker::kPrefilterMips;
                return Ptr<AssetDescriptor>(desc);
            }();
            return instance;
        }
        default:
        {
            static Ptr<AssetDescriptor> instance(new ImageAssetDescriptor{});
            return instance;
        }
        }
    }

    AssetId ImageAsset::MakeSubId(const AssetId& parentId, eastl::string_view subLabel,
                                  ImageUsage usage)
    {
        // A sub-asset of a sub-asset would drop the parent's own subLabel and could collide.
        ASSERT(!parentId.IsSubAsset(),
            "[ImageAsset] MakeSubId: parent is itself a sub-asset ('{}'); the sub id would "
            "lose its label", parentId.GetPath().c_str());

        const eastl::string& parentPath = parentId.GetPath();
        return AssetId::OfSub<ImageAsset>(
            eastl::string_view(parentPath.c_str(), parentPath.size()),
            subLabel,
            static_cast<const ImageAssetDescriptor&>(*DescriptorForUsage(usage)));
    }

    uint32_t ImageAsset::ComputeMipLevels(uint32_t width, uint32_t height, uint32_t maxLevel)
    {
        uint32_t maxDim = eastl::max(width, height);
        uint32_t fullMips = 1;

        while (maxDim > 1)
        {
            maxDim >>= 1;
            ++fullMips;
        }

        if (maxLevel == 0)
        {
            return fullMips;
        }

        return eastl::min(fullMips, maxLevel);
    }

    Ptr<AssetDescriptor> ImageAsset::DefaultDescriptor()
    {
        return DescriptorForUsage(ImageUsage::Texture2D);
    }

    Ptr<AssetDescriptor> ImageAsset::DefaultHDRDescriptor()
    {
        return DescriptorForUsage(ImageUsage::EnvironmentCubemap);
    }

    ImageAsset::ImageAsset(AssetId id)
        : Asset(eastl::move(id))
    {}

    const ImageAssetData* ImageAsset::GetImageData() const
    {
        return GetData<ImageAssetData>();
    }

    Ptr<ImageAsset> ImageAsset::GetIrradianceAsset() const
    {
        auto* data = GetImageData();
        return data ? data->GetIrradianceAsset() : nullptr;
    }

    Ptr<ImageAsset> ImageAsset::GetPrefilteredAsset() const
    {
        auto* data = GetImageData();
        return data ? data->GetPrefilteredAsset() : nullptr;
    }

    int ImageAsset::GetWidth() const
    {
        auto* data = GetImageData();
        return data ? data->GetWidth() : 0;
    }

    int ImageAsset::GetHeight() const
    {
        auto* data = GetImageData();
        return data ? data->GetHeight() : 0;
    }

    uint32_t ImageAsset::GetMipLevels() const
    {
        auto* data = GetImageData();
        return data ? data->GetMipLevels() : 0;
    }

    RHI::Format ImageAsset::GetFormat() const
    {
        auto* data = GetImageData();
        return data ? data->GetFormat() : RHI::Format::Unknown;
    }

    eastl::string_view ImageAsset::GetPath() const
    {
        return GetAssetId().GetPath();
    }
}
