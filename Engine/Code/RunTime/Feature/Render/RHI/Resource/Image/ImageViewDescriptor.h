#pragma once

#include <EASTL/functional.h>

#include <Format.h>
#include "ImageEnums.h"

namespace Spark::Render::RHI
{
    //! Image views map to a range of mips / array slices in an image.
    struct ImageViewDescriptor
    {
        //! Creates a view with a custom format and mip chain range.
        static ImageViewDescriptor Create(
            Format overrideFormat,
            uint16_t mipSliceMin,
            uint16_t mipSliceMax);

        //! Creates a view with a custom format, mip slice range, and array slice range.
        static ImageViewDescriptor Create(
            Format overrideFormat,
            uint16_t mipSliceMin,
            uint16_t mipSliceMax,
            uint16_t arraySliceMin,
            uint16_t arraySliceMax);

        //! Creates a default view that maps to the full subresource range, but is
        //! set to interpret the array slices as cubemap faces.
        static ImageViewDescriptor CreateCubemap();

        //! Creates a cubemap view with a specific format and mip slice range.
        static ImageViewDescriptor CreateCubemap(
            Format overrideFormat,
            uint16_t mipSliceMin,
            uint16_t mipSliceMax);

        //! Creates a cubemap view with a specific format, mip slice range, and array slice range.
        static ImageViewDescriptor CreateCubemap(
            Format overrideFormat,
            uint16_t mipSliceMin,
            uint16_t mipSliceMax,
            uint16_t cubeSliceMin,
            uint16_t cubeSliceMax);

        //! Creates a cubemap face view with a specific format and mip slice range.
        static ImageViewDescriptor CreateCubemapFace(
            Format overrideFormat,
            uint16_t mipSliceMin,
            uint16_t mipSliceMax,
            uint16_t faceSlice);

        //! Create a view for 3d texture
        static ImageViewDescriptor Create3D(
            Format overrideFormat,
            uint16_t mipSliceMin,
            uint16_t mipSliceMax,
            uint16_t depthSliceMin,
            uint16_t depthSliceMax);

        static const uint16_t HighestSliceIndex = static_cast<uint16_t>(-1);

        ImageViewDescriptor() = default;
        explicit ImageViewDescriptor(Format overrideFormat);
        bool operator==(const ImageViewDescriptor& other) const;
        bool operator!=(const ImageViewDescriptor& other) const;

        // Returns true if other is the same sub resource
        bool IsSameSubResource(const ImageViewDescriptor& other) const;

        //! Return true if any subresource overlaps with another ImageViewDescriptor
        bool OverlapsSubResource(const ImageViewDescriptor& other) const;
            
        /// Minimum mip slice offset.
        uint16_t m_mipSliceMin = 0;

        /// Maximum mip slice offset. Must be greater than or equal to the min mip slice offset.
        uint16_t m_mipSliceMax = HighestSliceIndex;

        /// Minimum array slice offset.
        uint16_t m_arraySliceMin = 0;

        /// Maximum array slice offset. Must be greater or equal to the min array slice offset.
        uint16_t m_arraySliceMax = HighestSliceIndex;

        /// Minimum depth slice offset.
        uint16_t m_depthSliceMin = 0;

        /// Maximum depth slice offset. Must be greater or equal to the min depth slice offset.
        uint16_t m_depthSliceMax = HighestSliceIndex;

        /// Typed format of the view, which may override a format in
        /// the image. A value of Unknown will default to the image format.
        Format m_overrideFormat = Format::Unknown;

        /// The bind flags used by this view. Should be compatible with the bind flags of the underlying image.
        ImageBindFlags m_overrideBindFlags = ImageBindFlags::None;

        /// Whether to treat this image as a cubemap in the shader
        uint32_t m_isCubemap = 0;

        /// Aspects of the image accessed by the view.
        ImageAspectFlags m_aspectFlags = ImageAspectFlags::All;

        /// Whether to treat this image as texture array.
        /// This is needed because a texture array can have 1 layer only.
        uint32_t m_isArray = 0;            
    };

    struct ImageViewDescriptoHasher {
        size_t operator()(const ImageViewDescriptor& descriptor) const {
            size_t h1 = eastl::hash<uint32_t>()(descriptor.m_mipSliceMin);
            size_t h2 = eastl::hash<uint32_t>()(descriptor.m_mipSliceMax);
            size_t h3 = eastl::hash<uint32_t>()(descriptor.m_arraySliceMin);
            size_t h4 = eastl::hash<uint32_t>()(descriptor.m_arraySliceMax);
            size_t h5 = eastl::hash<uint32_t>()(descriptor.m_depthSliceMin);
            size_t h6 = eastl::hash<uint32_t>()(descriptor.m_depthSliceMax);
            size_t h7 = eastl::hash<uint32_t>()(static_cast<uint32_t>(descriptor.m_overrideFormat));
            size_t h8 = eastl::hash<uint32_t>()(static_cast<uint32_t>(descriptor.m_overrideBindFlags));
            size_t h9 = eastl::hash<uint32_t>()(descriptor.m_isCubemap);
            size_t h10 = eastl::hash<uint32_t>()(static_cast<uint32_t>(descriptor.m_aspectFlags));
            size_t h11 = eastl::hash<uint32_t>()(descriptor.m_isArray);

            // Combine the hash values
            size_t combinedHash = h1;
            combinedHash ^= h2 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
            combinedHash ^= h3 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
            combinedHash ^= h4 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
            combinedHash ^= h5 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
            combinedHash ^= h6 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
            combinedHash ^= h7 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
            combinedHash ^= h8 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
            combinedHash ^= h9 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
            combinedHash ^= h10 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
            combinedHash ^= h11 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);

            return combinedHash;
        }
    };
}