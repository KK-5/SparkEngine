#include "ImageSubResource.h"

#include <Log/SpdLogSystem.h>

#include "ImageDescriptor.h"
#include "ImageViewDescriptor.h"

namespace Spark::RHI
{
    ImageSubresource::ImageSubresource(uint16_t mipSlice, uint16_t arraySlice)
        : m_mipSlice{mipSlice}
        , m_arraySlice{arraySlice}
    {}

    ImageSubresource::ImageSubresource(uint16_t mipSlice, uint16_t arraySlice, ImageAspect aspect)
        : m_mipSlice{ mipSlice }
        , m_arraySlice{ arraySlice }
        , m_aspect{ aspect }
    {}

    ImageSubresourceRange::ImageSubresourceRange(
        uint16_t mipSliceMin,
        uint16_t mipSliceMax,
        uint16_t arraySliceMin,
        uint16_t arraySliceMax)
        : m_mipSliceMin{mipSliceMin}
        , m_mipSliceMax{mipSliceMax}
        , m_arraySliceMin{arraySliceMin}
        , m_arraySliceMax{arraySliceMax}
    {}

    ImageSubresourceRange::ImageSubresourceRange(const ImageDescriptor& descriptor)
        : m_mipSliceMin{ 0 }
        , m_mipSliceMax{ static_cast<uint16_t>(descriptor.m_mipLevels - 1) }
        , m_arraySliceMin{ 0 }
        , m_arraySliceMax{ static_cast<uint16_t>(descriptor.m_arraySize - 1) }
        , m_aspectFlags{ GetImageAspectFlags(descriptor.m_format) }
    {
    }

    ImageSubresourceRange::ImageSubresourceRange(const ImageViewDescriptor& descriptor)
        : m_mipSliceMin{ descriptor.m_mipSliceMin }
        , m_mipSliceMax{ descriptor.m_mipSliceMax }
        , m_arraySliceMin{ descriptor.m_arraySliceMin }
        , m_arraySliceMax{ descriptor.m_arraySliceMax }
        , m_aspectFlags{ descriptor.m_aspectFlags }
    {
    }

    bool ImageSubresourceRange::operator==(const ImageSubresourceRange& other) const
    {
        return
            m_mipSliceMin == other.m_mipSliceMin &&
            m_mipSliceMax == other.m_mipSliceMax &&
            m_arraySliceMin == other.m_arraySliceMin &&
            m_arraySliceMax == other.m_arraySliceMax &&
            m_aspectFlags == other.m_aspectFlags;
    }

    ImageSubresourceLayout::ImageSubresourceLayout(
        Size size, 
        uint32_t rowCount,
        uint32_t bytesPerRow,
        uint32_t bytesPerImage,
        uint32_t blockElementWidth,
        uint32_t blockElementHeight,
        uint32_t offset)
        : m_size{size}
        , m_rowCount{rowCount}
        , m_bytesPerRow{bytesPerRow}
        , m_bytesPerImage{bytesPerImage}
        , m_blockElementWidth{blockElementWidth}
        , m_blockElementHeight{blockElementHeight}
        , m_offset{offset}
    {}

    ImageSubresourceLayout GetImageSubresourceLayout(Size imageSize, Format imageFormat)
    {
        ImageSubresourceLayout subresourceLayout;
        bool isBlockCompressed = false;
        bool isPacked = false;
        bool isPlanar = false;
        uint32_t bytesPerElement = 0; //For block compressions this is bytesPerBlock
        uint32_t numBlocks = 0;
        switch (imageFormat)
        {
        case Format::R32G32B32A32_FLOAT:
        case Format::R32G32B32A32_UINT:
        case Format::R32G32B32A32_SINT:
        case Format::R32G32B32_FLOAT:
        case Format::R32G32B32_UINT:
        case Format::R32G32B32_SINT:
        case Format::R16G16B16A16_FLOAT:
        case Format::R16G16B16A16_UNORM:
        case Format::R16G16B16A16_UINT:
        case Format::R16G16B16A16_SNORM:
        case Format::R16G16B16A16_SINT:
        case Format::R32G32_FLOAT:
        case Format::R32G32_UINT:
        case Format::R32G32_SINT:
        case Format::D32_FLOAT_S8X24_UINT:
        case Format::R10G10B10A2_UNORM:
        case Format::R10G10B10A2_UINT:
        case Format::R11G11B10_FLOAT:
        case Format::R8G8B8A8_UNORM:
        case Format::R8G8B8A8_UNORM_SRGB:
        case Format::R8G8B8A8_UINT:
        case Format::R8G8B8A8_SNORM:
        case Format::R8G8B8A8_SINT:
        case Format::R16G16_FLOAT:
        case Format::R16G16_UNORM:
        case Format::R16G16_UINT:
        case Format::R16G16_SNORM:
        case Format::R16G16_SINT:
        case Format::D32_FLOAT:
        case Format::R32_FLOAT:
        case Format::R32_UINT:
        case Format::R32_SINT:
        case Format::D24_UNORM_S8_UINT:
        case Format::B8G8R8A8_UNORM:
        case Format::B8G8R8X8_UNORM:
        case Format::B8G8R8A8_UNORM_SRGB:
        case Format::B8G8R8X8_UNORM_SRGB:
        case Format::R8G8_UNORM:
        case Format::R8G8_UINT:
        case Format::R8G8_SNORM:
        case Format::R8G8_SINT:
        case Format::R16_FLOAT:
        case Format::D16_UNORM:
        case Format::R16_UNORM:
        case Format::R16_UINT:
        case Format::R16_SNORM:
        case Format::R16_SINT:
        case Format::R8_UNORM:
        case Format::R8_UINT:
        case Format::R8_SNORM:
        case Format::R8_SINT:
        case Format::A8_UNORM:
        case Format::R1_UNORM:
        case Format::R9G9B9E5_SHAREDEXP:
            break;

        case Format::BC1_UNORM:
        case Format::BC1_UNORM_SRGB:
        case Format::BC4_UNORM:
        case Format::BC4_SNORM:
            isBlockCompressed = true;
            bytesPerElement = 8;
            numBlocks = 4;
            break;

        case Format::BC2_UNORM:
        case Format::BC2_UNORM_SRGB:
        case Format::BC3_UNORM:
        case Format::BC3_UNORM_SRGB:
        case Format::BC5_UNORM:
        case Format::BC5_SNORM:
        case Format::BC6H_UF16:
        case Format::BC6H_SF16:
        case Format::BC7_UNORM:
        case Format::BC7_UNORM_SRGB:
        case Format::ASTC_4x4_UNORM:
        case Format::ASTC_4x4_UNORM_SRGB:
            isBlockCompressed = true;
            bytesPerElement = 16;
            numBlocks = 4;
            break;
                    
        case Format::ASTC_6x6_UNORM:
        case Format::ASTC_6x6_UNORM_SRGB:
            isBlockCompressed = true;
            bytesPerElement = 16;
            numBlocks = 6;
            break;
                    
        case Format::ASTC_8x8_UNORM:
        case Format::ASTC_8x8_UNORM_SRGB:
            isBlockCompressed = true;
            bytesPerElement = 16;
            numBlocks = 8;
            break;
                    
        case Format::ASTC_10x10_UNORM:
        case Format::ASTC_10x10_UNORM_SRGB:
            isBlockCompressed = true;
            bytesPerElement = 16;
            numBlocks = 10;
            break;
            
        case Format::ASTC_12x12_UNORM:
        case Format::ASTC_12x12_UNORM_SRGB:
            isBlockCompressed = true;
            bytesPerElement = 16;
            numBlocks = 12;
            break;
                    
        case Format::R8G8_B8G8_UNORM:
        case Format::G8R8_G8B8_UNORM:
        case Format::YUY2:
            isPacked = true;
            bytesPerElement = 4;
            break;

        case Format::Y210:
        case Format::Y216:
            isPacked = true;
            bytesPerElement = 8;
            break;

        case Format::NV12:
            break;

        case Format::P010:
        case Format::P016:
            isPlanar = true;
            bytesPerElement = 4;
            break;

        case Format::ETC2_UNORM:
        case Format::ETC2_UNORM_SRGB:
        case Format::ETC2A1_UNORM:
        case Format::ETC2A1_UNORM_SRGB:
            isBlockCompressed = true;
            bytesPerElement = 8;
            numBlocks = 4;
            break;

        case Format::ETC2A_UNORM:
        case Format::ETC2A_UNORM_SRGB:
            isBlockCompressed = true;
            bytesPerElement = 16;
            numBlocks = 4;
            break;

        case Format::EAC_R11_UNORM:
        case Format::EAC_R11_SNORM:
            isBlockCompressed = true;
            bytesPerElement = 8;
            numBlocks = 4;
            break;

        case Format::EAC_RG11_UNORM:
        case Format::EAC_RG11_SNORM:
            isBlockCompressed = true;
            bytesPerElement = 16;
            numBlocks = 4;
            break;

        default:
            ASSERT(false, "Unimplemented esoteric format {}.", static_cast<int>(imageFormat));
        }

        if (isBlockCompressed)
        {
            uint32_t numBlocksWide = 0;
            if (imageSize.m_width > 0)
            {
                numBlocksWide = eastl::max<uint32_t>(1, (imageSize.m_width + (numBlocks - 1)) / numBlocks);
            }
            uint32_t numBlocksHigh = 0;
            if (imageSize.m_height > 0)
            {
                numBlocksHigh = eastl::max<uint32_t>(1, (imageSize.m_height + (numBlocks - 1)) / numBlocks);
            }
            subresourceLayout.m_bytesPerRow = numBlocksWide * bytesPerElement;
            subresourceLayout.m_rowCount = numBlocksHigh;
            subresourceLayout.m_size.m_width = imageSize.m_width;
            subresourceLayout.m_size.m_height = imageSize.m_height;
            subresourceLayout.m_blockElementWidth = numBlocks;
            subresourceLayout.m_blockElementHeight = numBlocks;
        }
        else if (isPacked)
        {
            subresourceLayout.m_bytesPerRow = ((imageSize.m_width + 1) >> 1) * bytesPerElement;
            subresourceLayout.m_rowCount = imageSize.m_height;
            subresourceLayout.m_size.m_width = imageSize.m_width;
            subresourceLayout.m_size.m_height = imageSize.m_height;
        }
        else if (imageFormat == Format::NV11)
        {
            subresourceLayout.m_bytesPerRow = ((imageSize.m_width + 3) >> 2) * 4;
            subresourceLayout.m_rowCount = imageSize.m_height * 2;
            subresourceLayout.m_size.m_width = AlignUp(imageSize.m_width, 2);
            subresourceLayout.m_size.m_height = AlignUp(imageSize.m_height, 2);
        }
        else if (isPlanar)
        {
            subresourceLayout.m_bytesPerRow = ((imageSize.m_width + 1) >> 1) * bytesPerElement;
            subresourceLayout.m_rowCount = imageSize.m_height + ((imageSize.m_height + 1) >> 1);
            subresourceLayout.m_size.m_width = AlignUp(imageSize.m_width, 2);
            subresourceLayout.m_size.m_height = AlignUp(imageSize.m_height, 2);
        }
        else
        {
            subresourceLayout.m_bytesPerRow = imageSize.m_width * GetFormatSize(imageFormat);
            subresourceLayout.m_rowCount = imageSize.m_height;
            subresourceLayout.m_size.m_width = imageSize.m_width;
            subresourceLayout.m_size.m_height = imageSize.m_height;
        }

        subresourceLayout.m_bytesPerImage = subresourceLayout.m_bytesPerRow * subresourceLayout.m_rowCount;
        subresourceLayout.m_size.m_depth = imageSize.m_depth;

        return subresourceLayout;
    }

    ImageSubresourceLayout GetImageSubresourceLayout(const ImageDescriptor& imageDescriptor, const ImageSubresource& subresource)
    {
        return GetImageSubresourceLayout(imageDescriptor.m_size.GetReducedMip(subresource.m_mipSlice), imageDescriptor.m_format);
    }

    uint32_t GetImageSubresourceIndex(uint32_t mipSlice, uint32_t arraySlice, uint32_t mipLevels)
    {
        return mipSlice + arraySlice * mipLevels;
    }

    uint32_t GetImageSubresourceIndex(ImageSubresource subresource, uint32_t mipLevels)
    {
        return GetImageSubresourceIndex(subresource.m_mipSlice, subresource.m_arraySlice, mipLevels);
    }
}