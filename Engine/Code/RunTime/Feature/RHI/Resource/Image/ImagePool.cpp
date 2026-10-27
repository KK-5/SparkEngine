#include "ImagePool.h"

#include <Log/ILogSystem.h>
#include <Math/Bit.h>

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

            // What both backends map the same way. A volume needs DX12 tiled resources tier 3,
            // a multisampled one optional Vulkan features, and Vulkan may bind a depth / stencil
            // format's two aspects apart. Packed mips are the backend's to find.
            // A tile backing is held to the same, being memory for such images.
            const ImageDescriptor& descriptor = initRequest.m_descriptor;
            const bool tileBacking = CheckBitsAny(descriptor.m_bindFlags, ImageBindFlags::TileBacking);
            if (descriptor.m_reserved || tileBacking)
            {
                const ImageAspectFlags aspects = GetImageAspectFlags(descriptor.m_format);
                const bool oneAspect = aspects == ImageAspectFlags::Color
                    || aspects == ImageAspectFlags::Depth
                    || aspects == ImageAspectFlags::Stencil;
                if (descriptor.m_dimension != ImageDimension::Image2D || descriptor.m_isCubemap
                    || descriptor.m_multisampleState.m_samples != 1 || !oneAspect)
                {
                    LOG_ERROR("[ImagePool] Reserved image or tile backing {} in pool {} must be a 2D image or array, "
                              "not a cubemap, of one sample and a format of one aspect.",
                              initRequest.m_image->GetName().GetCStr(), GetName().GetCStr());
                    return false;
                }
            }

            // Tiles of memory, counted along its one mip.
            if (tileBacking && (descriptor.m_reserved || descriptor.m_mipLevels != 1))
            {
                LOG_ERROR("[ImagePool] Tile backing {} in pool {} must have one mip and not be reserved.",
                          initRequest.m_image->GetName().GetCStr(), GetName().GetCStr());
                return false;
            }
        }

        return true;
    }

}