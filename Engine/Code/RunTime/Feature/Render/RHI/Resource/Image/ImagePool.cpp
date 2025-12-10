#include "ImagePool.h"

#include <Log/SpdLogSystem.h>

namespace Spark::Render::RHI
{
    ResultCode ImagePool::Init(Device& device, const ImagePoolDescriptor& descriptor)
    {
        return ResourcePool::Init(
            device, descriptor,
            [this, &device, &descriptor]()
        {
            m_descriptor = descriptor;
            return InitInternal(device, descriptor);
        });
    }

    ResultCode ImagePool::InitImage(const ImageInitRequest& initRequest)
    {
        return ImagePoolBase::InitImage(
            initRequest.m_image,
            initRequest.m_descriptor,
            [this, &initRequest]() { return InitImageInternal(initRequest); });
    }

    ResultCode ImagePool::UpdateImageContents(const ImageUpdateRequest& request)
    {
        if (!ValidateIsInitialized() || !ValidateNotProcessingFrame())
        {
            return ResultCode::InvalidOperation;
        }

        if (!IsRegistered(request.m_image))
        {
            return ResultCode::InvalidArgument;
        }

        if (!ValidateUpdateRequest(request))
        {
            return ResultCode::InvalidArgument;
        }

        return UpdateImageContentsInternal(request);
    }

    const ImagePoolDescriptor& ImagePool::GetDescriptor() const
    {
        return m_descriptor;
    }

    bool ImagePool::ValidateUpdateRequest(const ImageUpdateRequest& updateRequest) const
    {
        if (Validation::isEnabled)
        {
            const ImageDescriptor& imageDescriptor = updateRequest.m_image->GetDescriptor();
            if (updateRequest.m_imageSubresource.m_mipSlice >= imageDescriptor.m_mipLevels ||
                updateRequest.m_imageSubresource.m_arraySlice >= imageDescriptor.m_arraySize)
            {
                LOG_ERROR("[ImagePool]"
                    "Updating subresource (array: {}, mip: {}), but the image dimensions are (arraySize: {}, mipLevels: {})",
                    updateRequest.m_imageSubresource.m_mipSlice,
                    updateRequest.m_imageSubresource.m_arraySlice,
                    imageDescriptor.m_arraySize,
                    imageDescriptor.m_mipLevels);
                return false;
            }
        }

        return true;
    }
}