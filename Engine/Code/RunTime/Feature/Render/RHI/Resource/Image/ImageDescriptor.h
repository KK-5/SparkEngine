#pragma once

#include <assert.h>

#include <MultisampleState.h>
#include <Format.h>
#include <HardwareQueue.h>
#include <Size.h>
#include "ImageEnums.h"

namespace Spark::Render::RHI
{
    struct ImageDescriptor
    {
        static const uint16_t NumCubeMapSlices = 6;

        static ImageDescriptor Create1D(
            ImageBindFlags bindFlags,
            uint32_t width,
            Format format);

        static ImageDescriptor Create1DArray(
            ImageBindFlags bindFlags,
            uint32_t width,
            uint16_t arraySize,
            Format format);

        static ImageDescriptor Create2D(
            ImageBindFlags bindFlags,
            uint32_t width,
            uint32_t height,
            Format format);

        static ImageDescriptor Create2DArray(
            ImageBindFlags bindFlags,
            uint32_t width,
            uint32_t height,
            uint16_t arraySize,
            Format format);

        static ImageDescriptor CreateCubemap(
            ImageBindFlags bindFlags,
            uint32_t width,
            Format format);

        static ImageDescriptor CreateCubemapArray(
            ImageBindFlags bindFlags,
            uint32_t width,
            uint16_t arraySize,
            Format format);

        static ImageDescriptor Create3D(
            ImageBindFlags bindFlags,
            uint32_t width,
            uint32_t height,
            uint32_t depth,
            Format format);
        
        ImageDescriptor() = default;

        /// union of all bind points for this image
        ImageBindFlags m_bindFlags = ImageBindFlags::ShaderRead;

        /// Number of dimensions.
        ImageDimension m_dimension = ImageDimension::Image2D;

        /// Size of the image in pixels.
        Size m_size;

        /// Number of array elements (must be 1 for 3D images).
        uint16_t m_arraySize = 1;

        /// Number of mip levels.
        uint16_t m_mipLevels = 1;

        /// Pixel format.
        Format m_format = Format::Unknown;

        /// The mask of queue classes supporting shared access of this resource.
        HardwareQueueClassMask m_sharedQueueMask = HardwareQueueClassMask::All;

        /// Multisample information for this image.
        MultisampleState m_multisampleState;

        /// Whether to treat this image as a cubemap.
        uint32_t m_isCubemap = 0;
    };

    //! Returns whether mip 'A' is more detailed than mip 'B'.
    inline bool IsMipMoreDetailedThan(uint32_t mipA, uint32_t mipB)
    {
        return mipA < mipB;
    }

    //! Returns whether mip 'A' is less detailed than mip 'B'.
    inline bool IsMipLessDetailedThan(uint32_t mipA, uint32_t mipB)
    {
        return mipA > mipB;
    }

    //! Increases the mip detail by increaseBy levels.
    inline uint32_t IncreaseMipDetailBy(uint32_t mipLevel, uint32_t increaseBy)
    {
        assert(mipLevel >= increaseBy && "Exceeded mip detail.");
        return mipLevel - increaseBy;
    }

    //! Decreases the mip detail by decreaseBy levels.
    inline uint32_t DecreaseMipDetailBy(uint32_t mipLevel, uint32_t decreaseBy)
    {
        return mipLevel + decreaseBy;
    }
}