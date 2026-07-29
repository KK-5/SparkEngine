/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <RHI/Resource/Resource.h>
#include <RHI/HardwareQueue.h>
#include "ImageSubResource.h"
#include "ImageDescriptor.h"
#include "ImageViewDescriptor.h"

namespace Spark::RHI
{
    class ImageView;

    class Image : public Resource
    {
        friend class ImagePoolBase;
        friend class StreamingImagePool;
        friend class TransientResourcePool;
        friend class SwapChain;

    public:
        virtual ~Image() = default;

        //! Computes the subresource layouts and total size of the image contents, if represented linearly. Effectively,
        //! this data represents how to store the image in a buffer resource. Naturally, if the image contents
        //! are swizzled in device memory, the layouts will differ from the actual physical memory footprint. Use this data
        //! to facilitate transfers between buffers and images.
        //!
        //!  @param subresourceRange The range of subresources in the image to consider when computing subresource layouts.
        //!  @param subresourceLayouts
        //!      [Optional] If specified, fills the provided array with computed subresource layout results. Results are
        //!      written by GLOBAL subresource index (mip + arraySlice * mipLevels, see GetImageSubresourceIndex), NOT
        //!      packed into the range's own ordering. The array must therefore span the image's full mip*array grid
        //!      (m_mipLevels * m_arraySize) — sizing it by the range's subresource count overflows whenever the range
        //!      starts at a non-zero array slice.
        //!  @param totalSizeInBytes
        //!      [Optional] If specified, will be filled with the total size necessary to contain all subresources.
        void GetSubresourceLayouts(
            const ImageSubresourceRange& subresourceRange,
            ImageSubresourceLayout* subresourceLayouts,
            size_t* totalSizeInBytes) const;

        uint32_t GetResidentMipLevel() const;

        ImageAspectFlags GetAspectFlags() const;

        bool IsStreamable() const;

        const ImageDescriptor& GetDescriptor() const;

    protected:
        Image() = default;

        void SetDescriptor(const ImageDescriptor& descriptor);

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
    };
}