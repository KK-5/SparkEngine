/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <RHI/Base.h>
#include <RHI/Format.h>
#include <RHI/Attachment/AttachmentEnums.h>

namespace Spark::RHI
{
    using WindowHandle = uint64_t;

    struct SwapChainDimensions
    {
        /// Number of images in the swap chain.
        uint32_t m_imageCount = 0;

        /// Pixel width of the images in the swap chain.
        uint32_t m_imageWidth = 0;

        /// Pixel height of the images in the swap chain.
        uint32_t m_imageHeight = 0;

        /// Pixel format of the images in the swap chain.
        Format m_imageFormat = Format::Unknown;
    };

    class SwapChainDescriptor
    {
    public:
        virtual ~SwapChainDescriptor() = default;

        // The dimensions and format of the swap chain images.
        SwapChainDimensions m_dimensions;

        // 0: disable VSync. >= 1: sync N vertical blanks.
        uint32_t m_verticalSyncInterval = 0;

        // API dependent handle to the OS window to attach the swap chain.
        WindowHandle m_window;

         // ID for the SwapChain's attachment.
        AttachmentId m_attachmentId;

        // The scaling mode to use when presenting the swapchain's back buffer to the target
        // Note: not all platforms support stretch or stretch with aspect ratio.
        // Use DeviceFeature::m_swapChainScalingFlags to find out supported stretch modes
        Scaling m_scalingMode = RHI::Scaling::None;
    };
}