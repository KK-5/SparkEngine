#pragma once

#include <ClearValue.h>
#include <Origin.h>
#include "ImagePoolBase.h"
#include "ImagePoolDescriptor.h"
#include "Image.h"
#include "ImageSubresource.h"

namespace Spark::RHI
{
    struct ImageInitRequest
    {
        /// The image to initialize.
        Image* m_image = nullptr;

        /// The descriptor used to initialize the image.
        ImageDescriptor m_descriptor;

        /// An optional, optimized clear value for the image. Certain
        /// platforms may use this value to perform fast clears when this
        /// clear value is used.
        const ClearValue* m_optimizedClearValue = nullptr;
    };

    template <typename ImageClass, typename ImageSubresourceLayoutClass>
    struct ImageUpdateRequestTemplate
    {
        ImageUpdateRequestTemplate() = default;

        /// A pointer to an initialized image, whose contents will be updated.
        ImageClass* m_image = nullptr;

        /// The image subresource to update.
        ImageSubresource m_imageSubresource;

        /// The offset in pixels from the start of the sub-resource in the destination image.
        Origin m_imageSubresourcePixelOffset;

        /// The source data pointer
        const void* m_sourceData = nullptr;

        /// The source sub-resource layout.
        ImageSubresourceLayoutClass m_sourceSubresourceLayout;
    };

    using ImageUpdateRequest = ImageUpdateRequestTemplate<Image, ImageSubresourceLayout>;

    class ImagePool : public ImagePoolBase
    {
    public:
        virtual ~ImagePool() = default;

        ResultCode Init(Device& device, const ImagePoolDescriptor& descriptor);

        ResultCode InitImage(const ImageInitRequest& request);

        ResultCode UpdateImageContents(const ImageUpdateRequest& request);

        const ImagePoolDescriptor& GetDescriptor() const override final;
    
    protected:
        ImagePool() = default;
    
    private:
        using ResourcePool::Init;
        using ImagePoolBase::InitImage;

        bool ValidateUpdateRequest(const ImageUpdateRequest& updateRequest) const;

        //////////////////////////////////////////////////////////////////////////
        // Backend API

        /// Called when the pool is being initialized.
        virtual ResultCode InitInternal(Device& device, const ImagePoolDescriptor& descriptor) = 0;
        /// Called when an image contents are being updated.
        virtual ResultCode UpdateImageContentsInternal(const ImageUpdateRequest& request) = 0;
        /// Called when an image is being initialized on the pool.
        virtual ResultCode InitImageInternal(const ImageInitRequest& request) = 0;
        //////////////////////////////////////////////////////////////////////////

        ImagePoolDescriptor m_descriptor;
    };
}