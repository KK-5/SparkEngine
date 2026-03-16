/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- SwapChain no longer inherits from ImagePoolBase but directly from DeviceObject
 */

#include "SwapChain.h"

#include <Log/SpdLogSystem.h>

#include <RHI/Factory.h>
#include <RHI/Command/CommandQueue.h>

namespace Spark::RHI
{
    bool SwapChain::ValidateDescriptor(const SwapChainDescriptor& descriptor) const
    {
        if (Validation::isEnabled)
        {
            const bool isValidDescriptor =
                descriptor.m_dimensions.m_imageWidth != 0 &&
                descriptor.m_dimensions.m_imageHeight != 0 &&
                descriptor.m_dimensions.m_imageCount != 0;

            if (!isValidDescriptor)
            {
                LOG_WARN("[SwapChain] SwapChain display dimensions cannot be 0.");
                return false;
            }
        }

        return true;
    }

    ResultCode SwapChain::Init(RHI::Device& device, RHI::CommandQueue& commandQueue, const SwapChainDescriptor& descriptor)
    {
        if (!ValidateDescriptor(descriptor))
        {
            return ResultCode::InvalidArgument;
        }

        SetName(ObjectName{"SwapChain"});
        SwapChainDimensions nativeDimensions = descriptor.m_dimensions;

        ResultCode resultCode = InitInternal(device, commandQueue, descriptor, &nativeDimensions);

        if (resultCode == ResultCode::Success)
        {
            m_descriptor = descriptor;
            // Overwrite descriptor dimensions with the native ones (the ones assigned by the platform) returned by InitInternal.
            m_descriptor.m_dimensions = nativeDimensions;

            DeviceObject::Init(device);

            resultCode = InitImages();
        }

        return resultCode;
    }

    void SwapChain::Present()
    {
        // Due to swapchain recreation, the images are refreshed.
        // There is no need to present swapchain for this frame.
        const uint32_t imageCount = static_cast<uint32_t>(m_images.size());
        if (imageCount == 0)
        {
            return;
        }
        else
        {
            m_currentImageIndex = PresentInternal();
            ASSERT(m_currentImageIndex < imageCount, "Invalid image index");
        }
    }

    void SwapChain::SetVerticalSyncInterval(uint32_t verticalSyncInterval)
    {
        uint32_t previousVsyncInterval = m_descriptor.m_verticalSyncInterval;

        m_descriptor.m_verticalSyncInterval = verticalSyncInterval;

        SetVerticalSyncIntervalInternal(previousVsyncInterval);
    }

    ResultCode SwapChain::Resize(const SwapChainDimensions& dimensions)
    {
        ShutdownImages();

        SwapChainDimensions nativeDimensions = dimensions;
        ResultCode resultCode = ResizeInternal(dimensions, &nativeDimensions);
        if (resultCode == ResultCode::Success)
        {
            m_descriptor.m_dimensions = nativeDimensions;
            resultCode = InitImages();
        }

        return resultCode;
    }

    uint32_t SwapChain::GetImageCount() const
    {
        return static_cast<uint32_t>(m_images.size());
    }

    uint32_t SwapChain::GetCurrentImageIndex() const
    {
        return m_currentImageIndex;
    }

    Image* SwapChain::GetCurrentImage() const
    {
        return m_images[m_currentImageIndex].get();
    }

    Image* SwapChain::GetImage(uint32_t index) const
    {
        return m_images[index].get();
    }

    const AttachmentId& SwapChain::GetAttachmentId() const
    {
        return m_descriptor.m_attachmentId;
    }

    const SwapChainDescriptor& SwapChain::GetDescriptor() const
    {
        return m_descriptor;
    }

    void SwapChain::ShutdownImages()
    {
        // Shutdown existing set of images.
        uint32_t imageSize = static_cast<uint32_t>(m_images.size());
        for (uint32_t imageIdx = 0; imageIdx < imageSize; ++imageIdx)
        {
            ShutdownImageInternal(*m_images[imageIdx]);
        }

        m_images.clear();
    }

    ResultCode SwapChain::InitImages()
    {
        ResultCode resultCode = ResultCode::Success;

        m_images.reserve(m_descriptor.m_dimensions.m_imageCount);

        // If the new display mode has more buffers, add them.
        for (uint32_t i = 0; i < m_descriptor.m_dimensions.m_imageCount; ++i)
        {
            m_images.emplace_back(Service<Factory>::Get()->CreateImage());
        }

        InitImageRequest request;

        RHI::ImageDescriptor& imageDescriptor = request.m_descriptor;
        imageDescriptor.m_dimension = RHI::ImageDimension::Image2D;
        imageDescriptor.m_bindFlags = RHI::ImageBindFlags::Color;
        imageDescriptor.m_size.m_width = m_descriptor.m_dimensions.m_imageWidth;
        imageDescriptor.m_size.m_height = m_descriptor.m_dimensions.m_imageHeight;
        imageDescriptor.m_format = m_descriptor.m_dimensions.m_imageFormat;

        for (uint32_t imageIdx = 0; imageIdx < m_descriptor.m_dimensions.m_imageCount; ++imageIdx)
        {
            request.m_image = m_images[imageIdx].get();
            request.m_imageIndex = imageIdx;

            resultCode = InitImageInternal(request);

            if (resultCode != ResultCode::Success)
            {
                LOG_ERROR("[Swapchain] Failed to initialize images.");
                Shutdown();
                break;
            }
        }

        // Reset the current index back to 0 so we match the platform swap chain.
        m_currentImageIndex = 0;

        return resultCode;
    }

    void SwapChain::Shutdown()
    {
        ShutdownInternal();
        m_images.clear();
        DeviceObject::Shutdown();
    }
}