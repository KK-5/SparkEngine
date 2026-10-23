/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ImagePool.h"

#include <EASTL/vector.h>
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

    RHI::ResultCode ImagePool::InitInternal(RHI::Device& deviceBase, const RHI::ImagePoolDescriptor& descriptor)
    {
        Device& device = static_cast<Device&>(deviceBase);

        // Every image gets memory of its own.
        D3D12MA::POOL_DESC poolDesc = {};
        poolDesc.Flags               = D3D12MA::POOL_FLAG_ALWAYS_COMMITTED;
        poolDesc.HeapProperties.Type = ConvertHeapType(descriptor.m_heapMemoryLevel, descriptor.m_hostMemoryAccess);
        poolDesc.HeapFlags           = D3D12_HEAP_FLAG_NONE;

        ComPtr<D3D12MA::Pool> pool;
        if (FAILED(device.GetAllocator()->CreatePool(&poolDesc, &pool)))
        {
            LOG_ERROR("[ImagePool] Failed to create the D3D12MA pool of image pool {}.", GetName().GetCStr());
            return RHI::ResultCode::Fail;
        }
        m_pool = pool.Get();

        D3D12MAReleaseQueue::Descriptor releaseQueueDescriptor;
        releaseQueueDescriptor.m_collectLatency = device.GetDescriptor().m_frameCountMax;
        m_releaseQueue.Init(releaseQueueDescriptor);

        return RHI::ResultCode::Success;
    }

    void ImagePool::ShutdownInternal()
    {
        m_releaseQueue.Shutdown();
        // Last: D3D12MA releases a pool only once all its allocations are gone.
        m_pool.reset();
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

        D3D12MA::ALLOCATION_DESC allocDesc = {};
        allocDesc.CustomPool = m_pool.get();
        allocDesc.Flags = D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_STRATEGY_BEST_FIT;

        // Clear values only apply when the image is a render target or depth stencil.
        const bool isOutputMergerAttachment =
            CheckBitsAny(imageDesc.m_bindFlags, RHI::ImageBindFlags::Color | RHI::ImageBindFlags::DepthStencil);

        D3D12_CLEAR_VALUE clearValue;
        if (isOutputMergerAttachment && request.m_optimizedClearValue)
        {
            clearValue = ConvertClearValue(imageDesc.m_format, *request.m_optimizedClearValue);
        }

        // COMMON is usable on every queue, the copy queue's uploads included. The tracked
        // state starts at no access, so the first barrier discards from UNDEFINED anyway.
        const D3D12_RESOURCE_DESC1 resourceDesc1 = ConvertResourceDesc1(resourceDesc);
        ComPtr<D3D12MA::Allocation> allocation = nullptr;
        HRESULT result = device.GetAllocator()->CreateResource3(
            &allocDesc,
            &resourceDesc1,
            D3D12_BARRIER_LAYOUT_COMMON,
            (isOutputMergerAttachment && request.m_optimizedClearValue) ? &clearValue : nullptr,
            0,
            nullptr,
            &allocation,
            IID_NULL,
            NULL
        );

        if (FAILED(result))
        {
            LOG_ERROR("[ImagePool] D3D12MA Create image resource failed!");
            return RHI::ResultCode::Fail;
        }

        // Set debug name on the D3D12 resource for GPU debug tools (PIX / RenderDoc).
        {
            const ObjectName& debugName = image->GetName();
            if (!debugName.IsEmpty())
            {
                const char* utf8Name = debugName.GetCStr();
                const int len = MultiByteToWideChar(CP_UTF8, 0, utf8Name, -1, nullptr, 0);
                if (len > 0)
                {
                    eastl::vector<wchar_t> wideName(static_cast<size_t>(len));
                    MultiByteToWideChar(CP_UTF8, 0, utf8Name, -1, wideName.data(), len);
                    allocation->SetName(wideName.data());
                }
            }
        }

        MemoryView memoryView(allocation.Get(), MemoryViewType::Image, 0, allocation->GetSize(), allocation->GetAlignment());
        image->m_sizeInBytes = memoryView.GetSize();
        image->m_memoryView = eastl::move(memoryView);

        return RHI::ResultCode::Success;
    }

    void ImagePool::ShutdownResourceInternal(RHI::Resource& resourceBase)
    {
        Image& image = static_cast<Image&>(resourceBase);
        m_releaseQueue.QueueForCollect(image.GetMemoryView().GetMemoryAllocation());
        image.m_memoryView = {};
    }

    void ImagePool::OnFrameEnd()
    {
        m_releaseQueue.Collect();
        ResourcePool::OnFrameEnd();
    }
}