#pragma once

#include <RHI/Resource/Resource.h>
#include <RHI/Resource/ResourceViewCache.h>
#include <RHI/HardwareQueue.h>
#include "ImageSubResource.h"
#include "ImageDescriptor.h"
#include "ImageViewDescriptor.h"

namespace Spark::RHI
{
    class ImageFrameAttachment;
    class ImageView;

    class Image : public Resource
    {
        friend class ImagePoolBase;
        friend class StreamingImagePool;

    public:
        virtual ~Image() = default;

        void GetSubresourceLayouts(
            const ImageSubresourceRange& subresourceRange,
            ImageSubresourceLayout* subresourceLayouts,
            size_t* totalSizeInBytes) const;

        uint32_t GetResidentMipLevel() const;

        const ImageFrameAttachment* GetFrameAttachment() const;

        ImageAspectFlags GetAspectFlags() const;

        bool IsStreamable() const;

        const ImageDescriptor& GetDescriptor() const;

        Ptr<ImageView> GetImageView(const ImageViewDescriptor& imageViewDescriptor) const;

        void EraseImageView(ImageView* imageView) const;

        bool IsInImageCache(const ImageViewDescriptor& imageViewDescriptor);

    protected:
        Image() = default;

        virtual void SetDescriptor(const ImageDescriptor& descriptor);

    private:
        ///////////////////////////////////////////////////////////////////
        // Platform API

        /// Called by GetSubresourceLayouts. The subresource range is clamped and validated beforehand.
        virtual void GetSubresourceLayoutsInternal(
            const ImageSubresourceRange& subresourceRange,
            ImageSubresourceLayout* subresourceLayouts,
            size_t* totalSizeInBytes) const = 0;

        //! Returns whether the image has sub-resources which can be evicted from or streamed into the device memory
        virtual bool IsStreamableInternal() const { return false;};
        ///////////////////////////////////////////////////////////////////

        ImageDescriptor m_descriptor;

        HardwareQueueClassMask m_supportedQueueMask = HardwareQueueClassMask::All;

        uint32_t m_residentMipLevel = 0;

        ImageAspectFlags m_aspectFlags = ImageAspectFlags::None;

        mutable ResourceViewCache<ImageViewDescriptor, ImageViewDescriptoHasher> m_imageViewCache;
    };
}