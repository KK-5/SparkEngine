#include "ImagePool.h"

#include <Log/SpdLogSystem.h>

namespace Spark::RHI
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
        if (!ValidateInitRequest(initRequest))
        {
            return ResultCode::InvalidArgument;
        }

        return ImagePoolBase::InitImage(
            initRequest.m_image,
            initRequest.m_descriptor,
            [this, &initRequest]() { return InitImageInternal(initRequest); });
    }

    const ImagePoolDescriptor& ImagePool::GetDescriptor() const
    {
        return m_descriptor;
    }

    bool ImagePool::ValidateInitRequest(const ImageInitRequest& initRequest) const
    {
        if (Validation::isEnabled)
        {
            if ((GetDescriptor().m_bindFlags & initRequest.m_descriptor.m_bindFlags) != initRequest.m_descriptor.m_bindFlags)
            {
                LOG_ERROR("[ImagePool] Pool bind flags do not contain image bind flags in pool {}.", GetName().GetCStr());
                return false;
            }
        }

        return true;
    }

}