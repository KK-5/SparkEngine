/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ImageView.h"

#include <EASTL/algorithm.h>
#include <ID3D12Factory.h>
#include <Conversions.h>
#include <Descriptor/DescriptorContext.h>
#include <Device/Device.h>
#include "Image.h"

namespace Spark::RHI::DX12
{
    const Image& ImageView::GetImage() const
    {
        return static_cast<const Image&>(Base::GetImage());
    }

    DXGI_FORMAT ImageView::GetFormat() const
    {
        return m_format;
    }

    ID3D12Resource* ImageView::GetMemory() const
    {
        return m_memory;
    }

    DescriptorHandle ImageView::GetReadDescriptor() const
    {
        return m_readDescriptor;
    }

    DescriptorHandle ImageView::GetReadWriteDescriptor() const
    {
        return m_readWriteDescriptor;
    }

    DescriptorHandle ImageView::GetClearDescriptor() const
    {
        return m_clearDescriptor;
    }

    DescriptorHandle ImageView::GetColorDescriptor() const
    {
        return m_colorDescriptor;
    }

    DescriptorHandle ImageView::GetDepthStencilDescriptor() const
    {
        return m_depthStencilDescriptor;
    }

    DescriptorHandle ImageView::GetDepthStencilReadDescriptor() const
    {
        return m_depthStencilReadDescriptor;
    }

    uint32_t ImageView::GetBindlessReadIndex() const
    {
        return m_staticReadDescriptor.m_index;
    }

    uint32_t ImageView::GetBindlessReadWriteIndex() const
    {
        return m_staticReadWriteDescriptor.m_index;
    }

    RHI::ResultCode ImageView::InitInternal(RHI::Device& deviceBase, const RHI::Resource& resourceBase)
    {
        const Image& image = static_cast<const Image&>(resourceBase);

        RHI::ImageViewDescriptor viewDescriptor = GetDescriptor();
        viewDescriptor.m_mipSliceMax = eastl::min<uint16_t>(viewDescriptor.m_mipSliceMax, static_cast<uint16_t>(image.GetDescriptor().m_mipLevels - 1));

        // By default, if no bind flags are specified on the view descriptor, attempt to create all views that are compatible with the underlying image's bind flags
        // If bind flags are specified on the view descriptor, only create the views for the specified bind flags.
        bool hasOverrideBindFlags = viewDescriptor.m_overrideBindFlags != RHI::ImageBindFlags::None;
        const RHI::ImageBindFlags bindFlags = hasOverrideBindFlags ? viewDescriptor.m_overrideBindFlags : image.GetDescriptor().m_bindFlags;

        m_format = ConvertImageViewFormat(image, viewDescriptor);
        m_memory = image.GetMemoryView().GetMemory();

        Device& device = static_cast<Device&>(deviceBase);
        DescriptorContext& context = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);

        if (CheckBitsAny(bindFlags, RHI::ImageBindFlags::ShaderRead))
        {
            context.CreateShaderResourceView(image, viewDescriptor, m_readDescriptor, m_staticReadDescriptor);
            LOG_INFO("[ImageView] Image {} Create SRV {}", GetResource().GetName().GetCStr(), m_readDescriptor.m_index);
        }

        if (CheckBitsAny(bindFlags, RHI::ImageBindFlags::ShaderWrite))
        {
            context.CreateUnorderedAccessView(
                image, viewDescriptor, m_readWriteDescriptor, m_clearDescriptor, m_staticReadWriteDescriptor);
        }

        if (CheckBitsAny(bindFlags, RHI::ImageBindFlags::Color))
        {
            context.CreateRenderTargetView(image, viewDescriptor, m_colorDescriptor);
        }

        if (CheckBitsAny(bindFlags, RHI::ImageBindFlags::DepthStencil))
        {
            context.CreateDepthStencilView(
                image, viewDescriptor,
                m_depthStencilDescriptor,
                m_depthStencilReadDescriptor);
        }

        return RHI::ResultCode::Success;
    }

    void ImageView::ShutdownInternal()
    {
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& context = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);

        if (!m_readDescriptor.IsNull())
        {
            const uint32_t readIdx = m_readDescriptor.m_index;
            LOG_INFO("[ImageView] Image {} Release SRV {}", GetResource().GetName().GetCStr(), readIdx);
        }

        context.ReleaseDescriptor(m_readDescriptor);
        context.ReleaseDescriptor(m_readWriteDescriptor);
        context.ReleaseDescriptor(m_clearDescriptor);
        context.ReleaseDescriptor(m_colorDescriptor);
        context.ReleaseDescriptor(m_depthStencilDescriptor);
        context.ReleaseDescriptor(m_depthStencilReadDescriptor);
        context.ReleaseStaticDescriptor(m_staticReadDescriptor);
        context.ReleaseStaticDescriptor(m_staticReadWriteDescriptor);
        m_memory = nullptr;

        m_readDescriptor = DescriptorHandle();
        m_readWriteDescriptor = DescriptorHandle();
    }
}