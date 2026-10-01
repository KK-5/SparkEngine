#include "Image.h"
#include "ImageView.h"

#include <EASTL/algorithm.h>

#include <Math/Bit.h>
#include <RHI/Device/DeviceLimits.h>

namespace Spark::RHI
{
    void Image::GetSubresourceLayouts(
        const ImageSubresourceRange& subresourceRange,
        ImageSubresourceLayout* subresourceLayouts,
        size_t* totalSizeInBytes) const
    {
        const RHI::ImageDescriptor& imageDescriptor = GetDescriptor();
        const DeviceLimits& limits = GetDevice().GetLimits();

        const uint16_t mipSliceMax = eastl::clamp<uint16_t>(subresourceRange.m_mipSliceMax, subresourceRange.m_mipSliceMin, imageDescriptor.m_mipLevels - 1);
        const uint16_t arraySliceMax = eastl::clamp<uint16_t>(subresourceRange.m_arraySliceMax, subresourceRange.m_arraySliceMin, imageDescriptor.m_arraySize - 1);
        const uint32_t offsetAlignment = GetImageCopyOffsetAlignment(imageDescriptor.m_format, limits);

        uint32_t byteOffset = 0;
        for (uint16_t arraySlice = subresourceRange.m_arraySliceMin; arraySlice <= arraySliceMax; ++arraySlice)
        {
            for (uint16_t mipSlice = subresourceRange.m_mipSliceMin; mipSlice <= mipSliceMax; ++mipSlice)
            {
                ImageSubresourceLayout subresourceLayout =
                    GetImageSubresourceLayout(imageDescriptor, ImageSubresource(mipSlice, arraySlice));

                subresourceLayout.m_bytesPerRow = AlignUp(subresourceLayout.m_bytesPerRow, limits.m_imageCopyRowPitchAlignment);
                subresourceLayout.m_bytesPerImage = subresourceLayout.m_rowCount * subresourceLayout.m_bytesPerRow;
                subresourceLayout.m_offset = byteOffset;

                if (subresourceLayouts)
                {
                    subresourceLayouts[GetImageSubresourceIndex(mipSlice, arraySlice, imageDescriptor.m_mipLevels)] = subresourceLayout;
                }
                byteOffset = AlignUpNPOT(byteOffset + subresourceLayout.m_bytesPerImage * subresourceLayout.m_size.m_depth, offsetAlignment);
            }
        }

        if (totalSizeInBytes)
        {
            *totalSizeInBytes = byteOffset;
        }
    }

     uint32_t Image::GetResidentMipLevel() const
     {
        return m_residentMipLevel;
     }

    ImageAspectFlags Image::GetAspectFlags() const
    {
        return m_aspectFlags;
    }

    bool Image::IsStreamable() const
    {
        return IsStreamableInternal();
    }

    void Image::SetDescriptor(const ImageDescriptor& descriptor)
    {
        m_descriptor = descriptor;
        m_aspectFlags = GetImageAspectFlags(descriptor.m_format);
        // Pools set the initial state before the descriptor: it carries over.
        m_subresourceStates = ImageSubresourceStates(
            descriptor.m_mipLevels, descriptor.m_arraySize, m_aspectFlags, m_subresourceStates.GetUniform());
    }

    const ImageDescriptor& Image::GetDescriptor() const
    {
        return m_descriptor;
    }

    ResourceState Image::GetResourceState() const
    {
        return m_subresourceStates.GetUniform();
    }

    const ImageSubresourceStates& Image::GetSubresourceStates() const
    {
        return m_subresourceStates;
    }

    void Image::SetResourceState(ResourceState state)
    {
        m_subresourceStates.SetUniform(state);
    }
}