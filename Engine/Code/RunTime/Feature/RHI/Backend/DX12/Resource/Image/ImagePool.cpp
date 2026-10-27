/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ImagePool.h"

#include <Math/Bit.h>
#include <Service/Service.h>
#include <Conversions.h>
#include <Device/Device.h>
#include <ID3D12Factory.h>
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

        // Every image gets memory of its own, but a tile backing's is mapped from its heap, so a pool
        // that makes them places its images.
        D3D12MA::POOL_DESC poolDesc = {};
        poolDesc.Flags = CheckBitsAny(descriptor.m_bindFlags, RHI::ImageBindFlags::TileBacking)
            ? D3D12MA::POOL_FLAG_NONE
            : D3D12MA::POOL_FLAG_ALWAYS_COMMITTED;
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
        Image* image = static_cast<Image*>(request.m_image);

        const RHI::ImageDescriptor& imageDesc = request.m_descriptor;

        D3D12_RESOURCE_DESC resourceDesc;
        ConvertImageDescriptor(imageDesc, resourceDesc);

        // Clear values only apply when the image is a render target or depth stencil.
        const bool isOutputMergerAttachment = CheckBitsAny(resourceDesc.Flags,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        D3D12_CLEAR_VALUE clearValue;
        const D3D12_CLEAR_VALUE* optimizedClearValue = nullptr;
        if (isOutputMergerAttachment && request.m_optimizedClearValue)
        {
            clearValue = ConvertClearValue(imageDesc.m_format, *request.m_optimizedClearValue);
            optimizedClearValue = &clearValue;
        }

        return imageDesc.m_reserved
            ? InitReservedImage(*image, imageDesc, resourceDesc, optimizedClearValue)
            : InitUniqueImage(*image, resourceDesc, optimizedClearValue);
    }

    RHI::ResultCode ImagePool::InitReservedImage(Image& image, const RHI::ImageDescriptor& descriptor,
        D3D12_RESOURCE_DESC resourceDesc, const D3D12_CLEAR_VALUE* clearValue)
    {
        ID3D12DeviceX* dx12Device = GetDevice().GetDX12Device();

        // Asked of the format itself: the resource's may be its typeless family.
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
        support.Format = ConvertFormat(descriptor.m_format, false);
        if (FAILED(dx12Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))
            || !CheckBitsAll(support.Support2, D3D12_FORMAT_SUPPORT2_TILED))
        {
            LOG_ERROR("[ImagePool] Reserved image {} has format {}, which cannot be tiled.",
                      image.GetName().GetCStr(), static_cast<uint32_t>(descriptor.m_format));
            return RHI::ResultCode::InvalidArgument;
        }

        // D3D12MA does not create reserved resources, and a reserved texture is laid out in
        // 64KB tiles.
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

        // COMMON as for any other image: the first barrier discards from UNDEFINED anyway.
        ComPtr<ID3D12Resource> resource;
        HRESULT result = dx12Device->CreateReservedResource2(
            &resourceDesc,
            D3D12_BARRIER_LAYOUT_COMMON,
            clearValue,
            nullptr,
            0,
            nullptr,
            IID_PPV_ARGS(&resource)
        );
        if (FAILED(result))
        {
            LOG_ERROR("[ImagePool] Failed to create reserved image {}.", image.GetName().GetCStr());
            return RHI::ResultCode::Fail;
        }

        ImageTileLayout& layout = image.m_tileLayout;
        const UINT subresourceCount = static_cast<UINT>(descriptor.m_mipLevels) * descriptor.m_arraySize;
        layout.m_subresourceTiling.resize(subresourceCount);

        UINT                 tileCount = 0;
        D3D12_PACKED_MIP_INFO packedMips{};
        D3D12_TILE_SHAPE     tileShape{};
        UINT                 tilingCount = subresourceCount;
        dx12Device->GetResourceTiling(resource.Get(), &tileCount, &packedMips, &tileShape,
                                      &tilingCount, 0, layout.m_subresourceTiling.data());

        // Mapped another way on either backend; none of the images this maps today has any.
        if (packedMips.NumPackedMips > 0)
        {
            LOG_ERROR("[ImagePool] Reserved image {} has {} packed mip(s): a mip smaller than a tile cannot be "
                      "mapped yet.", image.GetName().GetCStr(), packedMips.NumPackedMips);
            layout = {};
            return RHI::ResultCode::InvalidArgument;
        }

        layout.m_tileSize          = RHI::Size(tileShape.WidthInTexels, tileShape.HeightInTexels, tileShape.DepthInTexels);
        layout.m_tileCount         = tileCount;
        layout.m_tileCountStandard = tileCount;
        layout.m_mipCount          = descriptor.m_mipLevels;
        layout.m_mipCountStandard  = descriptor.m_mipLevels;
        image.SetTileShape(layout.m_tileSize);

        // No allocation, so no heap: what tells it from a buffer backing its tiles.
        const size_t byteCount = static_cast<size_t>(tileCount) * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
        MemoryView memoryView(resource.Get(), MemoryViewType::Image, 0, byteCount, 0);
        image.m_sizeInBytes = memoryView.GetSize();
        image.m_memoryView  = eastl::move(memoryView);
        return RHI::ResultCode::Success;
    }

    RHI::ResultCode ImagePool::InitUniqueImage(Image& image, const D3D12_RESOURCE_DESC& resourceDesc,
        const D3D12_CLEAR_VALUE* clearValue)
    {
        // A committed resource of the image's own: no real pooling happens. Placing images in
        // dedicated heaps instead would put residency under our control at the heap level.
        D3D12MA::ALLOCATION_DESC allocDesc = {};
        allocDesc.CustomPool = m_pool.get();
        allocDesc.Flags = D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_STRATEGY_BEST_FIT;

        // COMMON is usable on every queue, the copy queue's uploads included. The tracked
        // state starts at no access, so the first barrier discards from UNDEFINED anyway.
        const D3D12_RESOURCE_DESC1 resourceDesc1 = ConvertResourceDesc1(resourceDesc);
        ComPtr<D3D12MA::Allocation> allocation = nullptr;
        HRESULT result = GetDevice().GetAllocator()->CreateResource3(
            &allocDesc,
            &resourceDesc1,
            D3D12_BARRIER_LAYOUT_COMMON,
            clearValue,
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

        MemoryView memoryView(allocation.Get(), MemoryViewType::Image, 0, allocation->GetSize(), allocation->GetAlignment());
        image.m_sizeInBytes = memoryView.GetSize();
        image.m_memoryView = eastl::move(memoryView);

        return RHI::ResultCode::Success;
    }

    void ImagePool::ShutdownResourceInternal(RHI::Resource& resourceBase)
    {
        Image& image = static_cast<Image&>(resourceBase);
        if (image.GetDescriptor().m_reserved)
        {
            // No D3D12MA allocation to queue: the resource itself waits out the GPU.
            Service<ID3D12FactoryInterface>::Get()->QueueForRelease(GetDevice(), image.GetMemoryView().GetMemory());
            image.m_tileLayout = {};
        }
        else
        {
            m_releaseQueue.QueueForCollect(image.GetMemoryView().GetMemoryAllocation());
        }
        image.m_memoryView = {};
    }

    void ImagePool::OnFrameEnd()
    {
        m_releaseQueue.Collect();
        ResourcePool::OnFrameEnd();
    }
}