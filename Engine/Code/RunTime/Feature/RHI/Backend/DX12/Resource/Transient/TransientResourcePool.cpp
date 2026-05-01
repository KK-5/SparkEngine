#include "TransientResourcePool.h"

#include <Log/SpdLogSystem.h>
#include <Math/Bit.h>
#include <EASTLEX/hash.h>

#include <Conversions.h>
#include <Device/Device.h>
#include <Resource/Image/Image.h>
#include <ID3D12Factory.h>

namespace Spark::RHI::DX12
{
    namespace
    {
        // 当 descriptor 中 image+buffer budget 都为 0 时使用的默认堆预算
        constexpr uint64_t DefaultHeapBudgetInBytes = 256ull * 1024 * 1024;

        uint64_t ResolveHeapBudget(const RHI::TransientResourcePoolDescriptor& descriptor)
        {
            uint64_t total = descriptor.m_imageBudgetInBytes + descriptor.m_bufferBudgetInBytes;
            if (total == 0)
            {
                total = DefaultHeapBudgetInBytes;
            }
            // heap 必须按 MSAA 对齐，否则后续放置 MSAA RT/DS 时
            // CreateAliasingResource 会以 E_INVALIDARG 失败
            return AlignUp(total, static_cast<uint64_t>(D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT));
        }
    }

    /*
    size_t TransientResourcePool::ImageCacheEntry::GetHash() const
    {
        size_t hash = eastl::hash<uint32_t>()(m_offset);
        eastl::hash_combine(hash, m_descriptor);
    }
    */


    Device& TransientResourcePool::GetDevice() const
    {
        return static_cast<Device&>(DeviceObject::GetDevice());
    }

    RHI::ResultCode TransientResourcePool::InitInternal(
        RHI::Device& deviceBase,
        const RHI::TransientResourcePoolDescriptor& descriptor)
    {
        Device& device = static_cast<Device&>(deviceBase);

        // 1. D3D12MA::Allocator
        D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
        allocatorDesc.Flags    = D3D12MA::ALLOCATOR_FLAG_NONE;
        allocatorDesc.pDevice  = device.GetDX12Device();
        allocatorDesc.pAdapter = device.GetPhysicalDevice().GetAdapter();

        ComPtr<D3D12MA::Allocator> pAllocator;
        if (FAILED(D3D12MA::CreateAllocator(&allocatorDesc, &pAllocator)))
        {
            LOG_ERROR("[TransientResourcePool] Failed to initialize D3D12MA::Allocator.");
            return RHI::ResultCode::Fail;
        }
        m_allocator = pAllocator.Get();

        D3D12_FEATURE_DATA_D3D12_OPTIONS options = m_allocator->GetD3D12Options();
        ASSERT(options.ResourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2,
               "TransientResourcePool requires D3D12_RESOURCE_HEAP_TIER_2.");
        if (options.ResourceHeapTier < D3D12_RESOURCE_HEAP_TIER_2)
        {
            LOG_ERROR("[TransientResourcePool] Device does not support D3D12_RESOURCE_HEAP_TIER_2 "
                      "(observed tier: {}). Pool cannot operate on this device.",
                      static_cast<int>(options.ResourceHeapTier));
            m_allocator.reset();
            return RHI::ResultCode::Fail;
        }

        // 申请 transient 整块 heap，单块、永不扩容、ALLOW_ALL_BUFFERS_AND_TEXTURES
        const uint64_t heapSize  = ResolveHeapBudget(descriptor);
        const uint64_t heapAlign = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;

        D3D12MA::ALLOCATION_DESC heapAllocDesc = {};
        heapAllocDesc.Flags          = D3D12MA::ALLOCATION_FLAG_NONE;
        heapAllocDesc.HeapType       = ConvertHeapType(descriptor.m_heapMemoryLevel,
                                                       RHI::HostMemoryAccess::Write);
        heapAllocDesc.ExtraHeapFlags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;

        D3D12_RESOURCE_ALLOCATION_INFO heapInfo = { heapSize, heapAlign };

        ComPtr<D3D12MA::Allocation> heapAllocation;
        if (FAILED(m_allocator->AllocateMemory(&heapAllocDesc, &heapInfo, &heapAllocation)))
        {
            LOG_ERROR("[TransientResourcePool] AllocateMemory failed (heap size {} bytes, align {} bytes).",
                      heapSize, heapAlign);
            m_allocator.reset();
            return RHI::ResultCode::Fail;
        }
        m_bucket.m_heap = heapAllocation.Get();

        //    配套 VirtualBlock —— heap 内 offset 簿记。
        //    用默认 best-fit 算法；不要 LINEAR：别名命中需要在中间复用旧 offset，
        //    LINEAR 模式只允许尾部分配，跟我们的需求直接冲突。
        D3D12MA::VIRTUAL_BLOCK_DESC vbDesc = {};
        vbDesc.Size  = heapSize;
        vbDesc.Flags = D3D12MA::VIRTUAL_BLOCK_FLAG_NONE;

        ComPtr<D3D12MA::VirtualBlock> virtualBlock;
        if (FAILED(D3D12MA::CreateVirtualBlock(&vbDesc, &virtualBlock)))
        {
            LOG_ERROR("[TransientResourcePool] CreateVirtualBlock failed.");
            m_bucket.m_heap.reset();
            m_allocator.reset();
            return RHI::ResultCode::Fail;
        }
        m_bucket.m_offsetBlock = virtualBlock.Get();

        D3D12MAReleaseQueue::Descriptor mqDesc;
        mqDesc.m_collectLatency = device.GetDescriptor().m_frameCountMax;
        m_releaseQueue.Init(mqDesc);

        return RHI::ResultCode::Success;
    }

    RHI::Image* TransientResourcePool::CreateImageInternal(
        const RHI::TransientImageCreateInfo& createInfo,
        const RHI::TransientAllocationFence& allocFence)
    {
        D3D12_RESOURCE_DESC resourceDesc;
        ConvertImageDescriptor(createInfo.m_descriptor, resourceDesc);

        Device& device = GetDevice();
        D3D12_RESOURCE_ALLOCATION_INFO allocationInfo = device.GetDX12Device()->GetResourceAllocationInfo(0, 1, &resourceDesc);

        uint32_t bestChainSlot = InvalidPlacementIndex;
        uint32_t index = 0;
        for (auto& tailIndex : m_bucket.m_chainTails)
        {
            const Placement& tail = m_bucket.m_placements[tailIndex];

            if (tail.m_discard.m_timelinePosition <= allocFence.m_timelinePosition &&
                tail.m_size >= allocationInfo.SizeInBytes &&
                IsAligned(tail.m_offset, allocationInfo.Alignment)
                )
            {
                // first-fit，add best-fit in future.
                bestChainSlot = index;
                break;
            }
            ++index;
        }

        const uint32_t newIndex = static_cast<uint32_t>(m_bucket.m_placements.size());

        if (bestChainSlot != InvalidPlacementIndex)
        {
            const uint32_t prevTailIdx = m_bucket.m_chainTails[bestChainSlot];
            Placement& prevTail = m_bucket.m_placements[prevTailIdx];

            Placement newPlacement;
            newPlacement.m_offset = prevTail.m_offset;  // 同 offset
            newPlacement.m_size = allocationInfo.SizeInBytes;
            newPlacement.m_alloc = allocFence;
            newPlacement.m_aliasedFrom = prevTailIdx;
            newPlacement.m_discard = RHI::TransientAllocationFence(allocFence.m_pipelines, RHI::InvalidTimelinePosition);
            // newPlacement.m_vAlloc 留空：本次复用别人预定的 offset，没向 VirtualBlock 申请
            m_bucket.m_placements.push_back(eastl::move(newPlacement));

            // 注意prevTail可能失效，这里重新取
            m_bucket.m_placements[prevTailIdx].m_aliasedTo = newIndex;
            m_bucket.m_chainTails[bestChainSlot] = newIndex;

            // 加入barrier
            RHI::TransientAliasingBarrier barrier;
            barrier.m_resourceBefore = prevTail.m_resource.get();
            m_aliasingBarriers[allocFence.m_timelinePosition].push_back(barrier);

        }
        else
        {
            D3D12MA::VIRTUAL_ALLOCATION_DESC vDesc;
            vDesc.Flags = D3D12MA::VIRTUAL_ALLOCATION_FLAG_NONE;
            vDesc.Size = allocationInfo.SizeInBytes;
            vDesc.Alignment = allocationInfo.Alignment;

            D3D12MA::VirtualAllocation vAlloc;
            UINT64 offset;
            HRESULT hr = m_bucket.m_offsetBlock->Allocate(&vDesc, &vAlloc, &offset);
            if (FAILED(hr))
            {
                // Create committed resource, now return null
                LOG_WARN("[TransientResourcePool] VirtualBlock allocation failed (size {} align {}). "
                    "Committed fallback not yet implemented.",
                    allocationInfo.SizeInBytes, allocationInfo.Alignment);
                return nullptr;
            }

            Placement newPlacement;
            newPlacement.m_offset = offset;
            newPlacement.m_size = allocationInfo.SizeInBytes;
            newPlacement.m_alloc = allocFence;
            newPlacement.m_discard = RHI::TransientAllocationFence(allocFence.m_pipelines, RHI::InvalidTimelinePosition);
            m_bucket.m_placements.push_back(eastl::move(newPlacement));

            m_bucket.m_chainTails.push_back(newIndex);
        }

        // Create resource for new Placement
        auto factory = Service<RHI::Factory>::Get();
        ASSERT(factory, "RHI::Factory is null when shutting down TransientResourcePool.");

        // 初始化为无效状态
        Ptr<RHI::Image> image = factory->CreateImage();
        SetResourceState(*image, RHI::ResourceState{});

        const bool isOutputMergerAttachment =
            CheckBitsAny(createInfo.m_descriptor.m_bindFlags, RHI::ImageBindFlags::Color | RHI::ImageBindFlags::DepthStencil);
        D3D12_CLEAR_VALUE clearValue;
        if (isOutputMergerAttachment && createInfo.m_optimizedClearValue)
        {
            clearValue = ConvertClearValue(createInfo.m_descriptor.m_format, *createInfo.m_optimizedClearValue);
        }

        const RHI::ResourceState resourceState = image->GetResourceState();
        D3D12_RESOURCE_STATES initialResourceState = ConvertImageAttachmentState(resourceState.m_usage, resourceState.m_access);

        auto& newPlacement = m_bucket.m_placements.back();
        
        RHI::ResultCode result = InitResource(image.get(), [&]() -> RHI::ResultCode 
        {
            ComPtr<ID3D12Resource> dx12Resource;
            HRESULT hr = m_allocator->CreateAliasingResource(
                m_bucket.m_heap.get(),
                newPlacement.m_offset,
                &resourceDesc,
                initialResourceState,
                (isOutputMergerAttachment && createInfo.m_optimizedClearValue) ? &clearValue : nullptr,
                IID_PPV_ARGS(&dx12Resource));
            if (FAILED(hr))
            {
                LOG_ERROR("[TransientResourcePool] CreateAliasingResource failed (HRESULT 0x{:X}).",
                        static_cast<uint32_t>(hr));
                return RHI::ResultCode::Fail;
            }

            Image* dx12Image = static_cast<Image*>(image.get());
            SetImageDescriptor(*image, createInfo.m_descriptor);
            MemoryView memoryView(dx12Resource.Get(), MemoryViewType::Image,
                        newPlacement.m_offset,
                        newPlacement.m_size,
                        allocationInfo.Alignment);
            dx12Image->m_residentSizeInBytes = memoryView.GetSize();
            dx12Image->m_memoryView = eastl::move(memoryView);
            dx12Image->GenerateSubresourceLayouts();
            dx12Image->InitSubresourceAttachmentState();
            return RHI::ResultCode::Success;
        });

        if (result != RHI::ResultCode::Success)
        {
            // committed fallback 的位置；现在直接返回。
            // 注意：scan 阶段已经 push 了 Placement，严格来说要回滚（pop_back + 还原 chain tail）
            // 但先用 ASSERT/LOG 占位，等真要 fallback 时再处理。
            return nullptr;
        }

        newPlacement.m_resource = image;

        if (bestChainSlot != InvalidPlacementIndex)
        {
            auto& curBarrier = m_aliasingBarriers[allocFence.m_timelinePosition].back();
            curBarrier.m_resourceAfter = image.get();
        }

        return image.get();
    }

    void TransientResourcePool::GetAliasingBarriersInternal(uint32_t timelinePosition, RHI::AliasingBarrierList& out) const
    {
        auto it = m_aliasingBarriers.find(timelinePosition);
        if (it != m_aliasingBarriers.end())
        {
            out = it->second;
            return;
        }
        out = {};
    }

    void TransientResourcePool::OnFrameBeginInternal()
    {
        m_aliasingBarriers.clear();
    }

    void TransientResourcePool::ShutdownInternal()
    {
        auto factory = Service<ID3D12FactoryInterface>::Get();
        ASSERT(factory, "ID3D12FactoryInterface is null when shutting down TransientResourcePool.");

        Device& device = GetDevice();

        // 跨批次缓存里的 placed resource 走 ID3D12Factory 的全局 ObjReleaseQueue，
        // GPU 完成最后一帧消费后真正释放
        for (auto& [hash, resources] : m_placedResourceCache)
        {
            for (auto& resource : resources)
            {
                if (resource)
                {
                    factory->QueueForRelease(device, eastl::move(resource));
                }
            }
        }
        m_placedResourceCache.clear();

        // VirtualBlock 内残留的 vAlloc 由其析构自动 Clear。
        m_bucket.m_placements.clear();
        m_bucket.m_offsetBlock.reset();
        m_aliasingBarriers.clear();

        if (m_bucket.m_heap)
        {
            m_releaseQueue.QueueForCollect(eastl::move(m_bucket.m_heap));
        }

        m_releaseQueue.Shutdown();
        m_allocator.reset();
    }
}
