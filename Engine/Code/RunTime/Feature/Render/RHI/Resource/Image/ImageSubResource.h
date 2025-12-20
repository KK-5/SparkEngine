#pragma once

#include <Format.h>
#include <Size.h>
#include "ImageEnums.h"

namespace Spark::RHI
{
    struct ImageDescriptor;
    struct ImageViewDescriptor;

    struct ImageSubresource
    {
        /// Defaults to the highest detail mip and first array element.
        ImageSubresource() = default;

        /// Constructs a subresource from a specific mip and array slice.
        ImageSubresource(uint16_t mipslice, uint16_t arraySlice);

        /// Constructs a subresource from a specific mip, array slice and image aspect.
        ImageSubresource(uint16_t mipslice, uint16_t arraySlice, ImageAspect aspect);

        /// The offset into the mip chain.
        uint16_t m_mipSlice = 0;

        /// The offset into the array of mip chains.
        uint16_t m_arraySlice = 0;

        /// The image aspect that is included in the subresource.
        ImageAspect m_aspect = ImageAspect::Color;
    };

    struct ImageSubresourceRange
    {
        static const uint16_t HighestSliceIndex = static_cast<uint16_t>(-1);

        //! Defaults to the full mapping of the image.
        ImageSubresourceRange() = default;

        //! Constructs a range from a [min, max] range for mips and array indices.
        ImageSubresourceRange(
            uint16_t mipSliceMin,
            uint16_t mipSliceMax,
            uint16_t arraySliceMin,
            uint16_t arraySliceMax);

        //! Constructs a range that covers the whole image.
        explicit ImageSubresourceRange(const ImageDescriptor& descriptor);

        //! Constructs a range that covers the same region as the image view.
        explicit ImageSubresourceRange(const ImageViewDescriptor& descriptor);

        //! Comparison operator.
        bool operator==(const ImageSubresourceRange& other) const;

        /// Minimum mip slice offset.
        uint16_t m_mipSliceMin = 0;

        /// Maximum mip slice offset. Must be greater than or equal to the min mip slice offset.
        uint16_t m_mipSliceMax = HighestSliceIndex;

        /// Minimum array slice offset.
        uint16_t m_arraySliceMin = 0;

        /// Maximum array slice offset. Must be greater or equal to the min array slice offset.
        uint16_t m_arraySliceMax = HighestSliceIndex;

        /// The image aspects that are included in the subresource range.
        ImageAspectFlags m_aspectFlags = ImageAspectFlags::All;
    };

    struct ImageSubresourceLayout
    {
        ImageSubresourceLayout() = default;
        ImageSubresourceLayout(
            Size size,
            uint32_t rowCount,
            uint32_t bytesPerRow,
            uint32_t bytesPerImage,
            uint32_t numBlocksWidth,
            uint32_t numBlocksHeight,
            uint32_t offset = 0);

        /// The size of the image subresource in pixels. Certain formats have alignment requirements.
        /// Block compressed formats are 4 pixel aligned. Other non-standard formats may be 2 pixel aligned.
        Size m_size;

        /// The number of rows in an image slice.
        uint32_t m_rowCount = 0;

        /// The number of bytes in a contiguous row of the image data.
        uint32_t m_bytesPerRow = 0;

        /// The number of bytes in a single image slice. 3D textures are comprised of m_size.m_depth image slices.
        uint32_t m_bytesPerImage = 0;
            
        /// The number of blocks in width based on the texture fomat
        uint32_t m_blockElementWidth = 1;
            
        /// The number of blocks in height based on the texture fomat
        uint32_t m_blockElementHeight = 1;

        /// The number of bytes that image date is offset in a buffer.
        uint32_t m_offset = 0;
    };

    ImageSubresourceLayout GetImageSubresourceLayout(Size imageSize, Format imageFormat);
    ImageSubresourceLayout GetImageSubresourceLayout(const ImageDescriptor& imageDescriptor, const ImageSubresource& subresource);

    uint32_t GetImageSubresourceIndex(uint32_t mipSlice, uint32_t arraySlice, uint32_t mipLevels);
    uint32_t GetImageSubresourceIndex(ImageSubresource subresource, uint32_t mipLevels);
}