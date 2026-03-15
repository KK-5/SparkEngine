#include "StreamingImagePool.h"

#include <Log/SpdLogSystem.h>
#include <Math/Bit.h>

#include "ImageEnums.h"

namespace Spark::RHI
{
    ResultCode StreamingImagePool::Init(Device& device, const StreamingImagePoolDescriptor& descriptor)
    {
        return ResourcePool::Init(
            device, descriptor,
            [this, &device, &descriptor]()
        {
            m_descriptor = descriptor;

            return InitInternal(device, descriptor);
        });
    }

    ResultCode StreamingImagePool::InitImage(const StreamingImageInitRequest& request)
    {
        if (!ValidateIsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        if (!ValidateInitRequest(request))
        {
            return ResultCode::InvalidArgument;
        }

        ResultCode resultCode = ImagePoolBase::InitImage(
            request.m_image.get(),
            request.m_descriptor,
            [this, &request]()
            {
                return InitImageInternal(request);
            });

        if (resultCode == ResultCode::Success)
        {
            // If initialization succeeded, assign the new resident mip level.
            request.m_image->m_residentMipLevel = static_cast<uint32_t>(request.m_descriptor.m_mipLevels - request.m_tailMipSlices.size());
        }

        LOG_WARN("[StreamingImagePool] Failed to initialize image.");
        return resultCode;
    }

    ResultCode StreamingImagePool::ExpandImage(const StreamingImageExpandRequest& request)
    {
        if (!ValidateIsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        if (!ValidateExpandRequest(request))
        {
            return ResultCode::InvalidArgument;
        }

        const ResultCode resultCode = ExpandImageInternal(request);
        if (resultCode == ResultCode::Success)
        {
            request.m_image->m_residentMipLevel -= static_cast<uint32_t>(request.m_mipSlices.size());
        }
        return resultCode;
    }

    ResultCode StreamingImagePool::TrimImage(Image& image, uint32_t targetMipLevel)
    {
        if (!ValidateIsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        if (!IsRegistered(&image))
        {
            return ResultCode::InvalidArgument;
        }

        if (image.m_residentMipLevel < targetMipLevel)
        {
            const ResultCode resultCode = TrimImageInternal(image, targetMipLevel);
            if (resultCode == ResultCode::Success)
            {
                image.m_residentMipLevel = targetMipLevel;
                //image.InvalidateViews();
            }
            return resultCode;
        }

        return RHI::ResultCode::Success;
    }

    const StreamingImagePoolDescriptor& StreamingImagePool::GetDescriptor() const
    {
        return m_descriptor;
    }
            
    bool StreamingImagePool::SupportTiledImage() const
    {
        return false;
    }

    bool StreamingImagePool::ValidateInitRequest(const StreamingImageInitRequest& initRequest) const
    {
        if (Validation::isEnabled)
        {
            if (initRequest.m_tailMipSlices.empty())
            {
                LOG_ERROR("[StreamingImagePool] No tail mip slices were provided. You must provide at least one tail mip slice.");
                return false;
            }

            if (initRequest.m_tailMipSlices.size() > initRequest.m_descriptor.m_mipLevels)
            {
                LOG_ERROR("[StreamingImagePool] Tail mip array exceeds the number of mip levels in the image.");
                return false;
            }

            // Streaming images are only allowed to update via the CPU.
            if (CheckBitsAny(
                static_cast<uint32_t>(initRequest.m_descriptor.m_bindFlags),
                static_cast<uint32_t>(ImageBindFlags::Color) | 
                static_cast<uint32_t>(ImageBindFlags::DepthStencil) | 
                static_cast<uint32_t>(ImageBindFlags::ShaderWrite))
                )
            {
                LOG_ERROR("[StreamingImagePool] Streaming images may only contain read-only bind flags.");
                return false;
            }
        }

        return true;
    }

    bool StreamingImagePool::ValidateExpandRequest(const StreamingImageExpandRequest& expandRequest) const
    {
        if (Validation::isEnabled)
        {
            if (!IsRegistered(expandRequest.m_image.get()))
            {
                return false;
            }

            if (expandRequest.m_image->GetResidentMipLevel() < expandRequest.m_mipSlices.size())
            {
                LOG_ERROR("[StreamingImagePool] Attempted to expand image more than the number of mips available.");
                return false;
            }
        }

        return true;
    }
}