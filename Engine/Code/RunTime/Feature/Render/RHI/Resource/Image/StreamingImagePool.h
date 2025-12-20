#pragma once

#include <EASTL/span.h>
#include <EASTL/functional.h>
#include <mutex>

#include "Image.h"
#include "StreamingImagePoolDescriptor.h"
#include "ImagePoolBase.h"

namespace Spark::RHI
{
    struct StreamingImageSubresourceData
    {
        const void* m_data = nullptr;
    };

    struct StreamingImageMipSlice
    {
        eastl::span<const StreamingImageSubresourceData> m_subresources;

        ImageSubresourceLayout m_subresourceLayout;
    };

    using CompleteCallback = eastl::function<void()>;

    struct StreamingImageInitRequest
    {
        StreamingImageInitRequest() = default;

        StreamingImageInitRequest(
            Ptr<Image> image, const ImageDescriptor& descriptor, eastl::span<const StreamingImageMipSlice> tailMipSlices)
            : m_image{ eastl::move(image) }
            , m_descriptor{ descriptor }
            , m_tailMipSlices{ tailMipSlices }
        {}

        Ptr<Image> m_image = nullptr;

        ImageDescriptor m_descriptor;

        eastl::span<const StreamingImageMipSlice> m_tailMipSlices;
    };

    template <typename ImageClass>
    struct StreamingImageExpandRequestTemplate
    {
        StreamingImageExpandRequestTemplate() = default;

        Ptr<ImageClass> m_image = nullptr;

        eastl::span<const StreamingImageMipSlice> m_mipSlices;

        bool m_waitForUpload = false;
            
        CompleteCallback m_completeCallback;
    };

    using StreamingImageExpandRequest = StreamingImageExpandRequestTemplate<Image>;

    class StreamingImagePool : public ImagePoolBase
    {
    public:
        virtual ~StreamingImagePool() = default;

        static const uint64_t ImagePoolMininumSizeInBytes = 16ul  * 1024 * 1024;
            
        ResultCode Init(Device& device, const StreamingImagePoolDescriptor& descriptor);

        ResultCode InitImage(const StreamingImageInitRequest& request);

        ResultCode ExpandImage(const StreamingImageExpandRequest& request);

        ResultCode TrimImage(Image& image, uint32_t targetMipLevel);

        const StreamingImagePoolDescriptor& GetDescriptor() const override final;
            
        bool SupportTiledImage() const;

    protected:
        StreamingImagePool() = default;

    private:
        using ResourcePool::Init;
        using ImagePoolBase::InitImage;

        bool ValidateInitRequest(const StreamingImageInitRequest& initRequest) const;
        bool ValidateExpandRequest(const StreamingImageExpandRequest& expandRequest) const;

        //////////////////////////////////////////////////////////////////////////
        // Backend API

        // Called when the pool is being initialized.
        virtual ResultCode InitInternal(Device& device, const StreamingImagePoolDescriptor& descriptor) = 0;

        // Called when an image is being initialized on the pool.
        virtual ResultCode InitImageInternal(const StreamingImageInitRequest& request) = 0;

        // Called when an image mips are being expanded.
        virtual ResultCode ExpandImageInternal(const StreamingImageExpandRequest& request) = 0;

        // Called when an image mips are being trimmed.
        virtual ResultCode TrimImageInternal(Image& image, uint32_t targetMipLevel) = 0;
                        
        // Return if it supports tiled image feature
        virtual bool SupportTiledImageInternal() const = 0;

        //////////////////////////////////////////////////////////////////////////

        StreamingImagePoolDescriptor m_descriptor;

        std::shared_mutex m_frameMutex;
    };
}