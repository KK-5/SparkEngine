#include "Image.h"
#include "ImageView.h"

namespace Spark::Render::RHI
{
    void Image::GetSubresourceLayouts(
        const ImageSubresourceRange& subresourceRange,
        ImageSubresourceLayout* subresourceLayouts,
        size_t* totalSizeInBytes) const
    {
        const RHI::ImageDescriptor& imageDescriptor = GetDescriptor();

        ImageSubresourceRange subresourceRangeClamped;
        subresourceRangeClamped.m_mipSliceMin = subresourceRange.m_mipSliceMin;
        subresourceRangeClamped.m_mipSliceMax = eastl::clamp<uint16_t>(subresourceRange.m_mipSliceMax, subresourceRange.m_mipSliceMin, imageDescriptor.m_mipLevels - 1);
        subresourceRangeClamped.m_arraySliceMin = subresourceRange.m_arraySliceMin;
        subresourceRangeClamped.m_arraySliceMax = eastl::clamp<uint16_t>(subresourceRange.m_arraySliceMax, subresourceRange.m_arraySliceMin, imageDescriptor.m_arraySize - 1);
        GetSubresourceLayoutsInternal(subresourceRangeClamped, subresourceLayouts, totalSizeInBytes);
    }

     uint32_t Image::GetResidentMipLevel() const
     {
        return m_residentMipLevel;
     }

    const ImageFrameAttachment* Image::GetFrameAttachment() const
    {
        return nullptr;
        //return static_cast<const ImageFrameAttachment*>(Resource::GetFrameAttachment());
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
    }

    const ImageDescriptor& Image::GetDescriptor() const
    {
        return m_descriptor;
    }

    Ptr<ImageView> Image::GetImageView(const ImageViewDescriptor& imageViewDescriptor) const
    {
        ResourceView* view = m_imageViewCache.GetResourceView(imageViewDescriptor);
        return static_cast<ImageView*>(view);
    }

    void Image::EraseImageView(ImageView* imageView) const
    {
        m_imageViewCache.EraseResourceView(imageView);
    }

    bool Image::IsInImageCache(const ImageViewDescriptor& imageViewDescriptor)
    {
        return m_imageViewCache.IsInResourceCache(imageViewDescriptor);
    }
}