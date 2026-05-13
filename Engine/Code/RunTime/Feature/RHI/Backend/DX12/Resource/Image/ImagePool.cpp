/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ImagePool.h"

#include <Math/Bit.h>
#include <Conversions.h>
#include <Device/Device.h>
#include "Image.h"

namespace Spark::RHI::DX12
{
    Device& ImagePool::GetDevice() const
    {
        return static_cast<Device&>(Base::GetDevice());
    }

    ImagePoolResolver* ImagePool::GetResolver()
    {
        return nullptr;
    }

    RHI::ResultCode ImagePool::InitInternal(RHI::Device& deviceBase, const RHI::ImagePoolDescriptor&)
    {
        Device& device = static_cast<Device&>(deviceBase);

        // ImagePool 分配的内存总是commited的
        D3D12MA::ALLOCATOR_DESC desc = {};
        desc.Flags = D3D12MA::ALLOCATOR_FLAG_ALWAYS_COMMITTED;
        desc.pDevice = device.GetDX12Device();
        desc.pAdapter = device.GetPhysicalDevice().GetAdapter();

        ComPtr<D3D12MA::Allocator> pAllocator;
        if (FAILED(D3D12MA::CreateAllocator(&desc, &pAllocator)))
        {
            LOG_ERROR("[ImagePool] Failed to initialize the D3D12MemoryAllocator.");
            return RHI::ResultCode::Fail;
        }
        m_d3dmaAllocator = pAllocator.Get();

        D3D12MAReleaseQueue::Descriptor releaseQueueDescriptor;
        releaseQueueDescriptor.m_collectLatency = device.GetDescriptor().m_frameCountMax;
        m_releaseQueue.Init(releaseQueueDescriptor);

        return RHI::ResultCode::Success;
    }

    void ImagePool::ShutdownInternal()
    {
        m_releaseQueue.Shutdown();
        m_d3dmaAllocator.reset();
    }

    RHI::ResultCode ImagePool::InitImageInternal(const RHI::ImageInitRequest& request)
    {
        Device& device = GetDevice();

        Image* image = static_cast<Image*>(request.m_image);

        RHI::ImageDescriptor imageDesc = request.m_descriptor;

        /**
         * Super simple implementation. Just creates a committed resource for the image. No
         * real pooling happening at the moment. Other approaches might involve creating dedicated
         * heaps and then placing resources onto those heaps. This will allow us to control residency
         * at the heap level.
         */

        D3D12_RESOURCE_DESC resourceDesc;
        ConvertImageDescriptor(imageDesc, resourceDesc);

        // Read heap type from pool descriptor (Device → DEFAULT, Host+Read → READBACK)
        const RHI::ImagePoolDescriptor& poolDesc = GetDescriptor();
        D3D12MA::ALLOCATION_DESC allocDesc = {};
        allocDesc.HeapType = ConvertHeapType(poolDesc.m_heapMemoryLevel, poolDesc.m_hostMemoryAccess);
        allocDesc.Flags = D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_STRATEGY_BEST_FIT;

        // Clear values only apply when the image is a render target or depth stencil.
        const bool isOutputMergerAttachment =
            CheckBitsAny(imageDesc.m_bindFlags, RHI::ImageBindFlags::Color | RHI::ImageBindFlags::DepthStencil);

        D3D12_CLEAR_VALUE clearValue;
        if (isOutputMergerAttachment && request.m_optimizedClearValue)
        {
            clearValue = ConvertClearValue(imageDesc.m_format, *request.m_optimizedClearValue);
        }

        const RHI::ResourceState resourceState = image->GetResourceState();
        D3D12_RESOURCE_STATES initialResourceState = ConvertImageAttachmentState(resourceState.m_usage, resourceState.m_access);

        ComPtr<D3D12MA::Allocation> allocation = nullptr;
        HRESULT result = m_d3dmaAllocator->CreateResource(
            &allocDesc,
            &resourceDesc,
            initialResourceState,
            (isOutputMergerAttachment && request.m_optimizedClearValue) ? &clearValue : nullptr,
            &allocation,
            IID_NULL,
            NULL
        );

        if (FAILED(result))
        {
            LOG_ERROR("[ImagePool] D3D12MA Create image resource failed!");
            return RHI::ResultCode::Fail;
        }

        MemoryView memoryView(allocation.Get(), MemoryViewType::Image, 0, allocation->GetSize(), allocation->GetAlignment());
        image->m_residentSizeInBytes = memoryView.GetSize();
        image->m_memoryView = eastl::move(memoryView);
        image->GenerateSubresourceLayouts();
        image->InitSubresourceAttachmentState();
        image->m_streamedMipLevel = image->GetResidentMipLevel();

        return RHI::ResultCode::Success;
    }

    RHI::ResultCode ImagePool::UpdateImageContentsInternal(const RHI::ImageUpdateRequest& request)
    {
        return RHI::ResultCode::InvalidOperation;
    }

    void ImagePool::ShutdownResourceInternal(RHI::Resource& resourceBase)
    {
        Image& image = static_cast<Image&>(resourceBase);
        m_releaseQueue.QueueForCollect(image.GetMemoryView().GetMemoryAllocation());
        image.m_memoryView = {};
        image.m_pendingResolves = 0;
    }

    void ImagePool::OnFrameEnd()
    {
        m_releaseQueue.Collect();
        ResourcePool::OnFrameEnd();
    }
}