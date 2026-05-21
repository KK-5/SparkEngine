#include "TransientResourcePool.h"

#include <Log/SpdLogSystem.h>
#include <Math/Bit.h>
#include <EASTLEX/hash.h>

#include <Conversions.h>
#include <Device/Device.h>
#include <Resource/Buffer/Buffer.h>
#include <Resource/Image/Image.h>
#include <ID3D12Factory.h>

namespace Spark::RHI::DX12
{
    namespace
    {
        constexpr uint64_t DefaultHeapBudgetInBytes = 256ull * 1024 * 1024;

        uint64_t ResolveHeapBudget(const RHI::TransientResourcePoolDescriptor& descriptor)
        {
            uint64_t total = descriptor.m_imageBudgetInBytes + descriptor.m_bufferBudgetInBytes;
            if (total == 0)
            {
                total = DefaultHeapBudgetInBytes;
            }
            // 必须按 MSAA 对齐，否则放置 MSAA RT/DS 时 CreateAliasingResource 会 E_INVALIDARG。
            return AlignUp(total, static_cast<uint64_t>(D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT));
        }

        // 字段级哈希避开 D3D12_RESOURCE_DESC 的 padding。cache 命中复用 ID3D12Resource
        // 时物理布局必须与本次请求完全一致，所有影响布局的字段都得进 hash。
        size_t HashResourceDesc(const D3D12_RESOURCE_DESC& d)
        {
            size_t h = 0;
            eastl::hash_combine(h, static_cast<uint32_t>(d.Dimension));
            eastl::hash_combine(h, static_cast<uint64_t>(d.Alignment));
            eastl::hash_combine(h, static_cast<uint64_t>(d.Width));
            eastl::hash_combine(h, static_cast<uint32_t>(d.Height));
            eastl::hash_combine(h, static_cast<uint32_t>(d.DepthOrArraySize));
            eastl::hash_combine(h, static_cast<uint32_t>(d.MipLevels));
            eastl::hash_combine(h, static_cast<uint32_t>(d.Format));
            eastl::hash_combine(h, static_cast<uint32_t>(d.SampleDesc.Count));
            eastl::hash_combine(h, static_cast<uint32_t>(d.SampleDesc.Quality));
            eastl::hash_combine(h, static_cast<uint32_t>(d.Layout));
            eastl::hash_combine(h, static_cast<uint32_t>(d.Flags));
            return h;
        }

        uint64_t MakeResourceCacheKey(uint64_t offset, size_t descHash)
        {
            size_t h = 0;
            eastl::hash_combine(h, offset);
            eastl::hash_combine_raw(h, descHash);
            return static_cast<uint64_t>(h);
        }
    }


    Device& TransientResourcePool::GetDevice() const
    {
        return static_cast<Device&>(DeviceObject::GetDevice());
    }

    RHI::ResultCode TransientResourcePool::InitInternal(
        RHI::Device& deviceBase,
        const RHI::TransientResourcePoolDescriptor& descriptor)
    {
        Device& device = static_cast<Device&>(deviceBase);

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

        D3D12MAReleaseQueue::Descriptor mqDesc;
        mqDesc.m_collectLatency = device.GetDescriptor().m_frameCountMax;
        m_releaseQueue.Init(mqDesc);

        // 每槽独立 heap，descriptor 的 budget 是「单帧 working set」，总 VRAM = budget × frameCountMax
        const uint32_t frameCountMax = device.GetDescriptor().m_frameCountMax;
        ASSERT(frameCountMax > 0, "TransientResourcePool requires frameCountMax > 0.");

        const uint64_t heapSize  = ResolveHeapBudget(descriptor);
        const uint64_t heapAlign = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;

        m_buckets.resize(frameCountMax);
        for (uint32_t i = 0; i < frameCountMax; ++i)
        {
            if (InitBucket(m_buckets[i], descriptor, heapSize, heapAlign) != RHI::ResultCode::Success)
            {
                m_buckets.clear();
                m_releaseQueue.Shutdown();
                m_allocator.reset();
                return RHI::ResultCode::Fail;
            }
        }

        // 首次 OnFrameBegin 推进到 slot 0
        m_currentSlot = frameCountMax - 1;

        return RHI::ResultCode::Success;
    }

    RHI::ResultCode TransientResourcePool::InitBucket(
        HeapBucket& bucket,
        const RHI::TransientResourcePoolDescriptor& descriptor,
        uint64_t heapSize,
        uint64_t heapAlign)
    {
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
            return RHI::ResultCode::Fail;
        }
        bucket.m_heap = heapAllocation.Get();

        // 别名命中需要在 heap 中段复用旧 offset，不能用 LINEAR 模式（只允许尾部分配）。
        D3D12MA::VIRTUAL_BLOCK_DESC vbDesc = {};
        vbDesc.Size  = heapSize;
        vbDesc.Flags = D3D12MA::VIRTUAL_BLOCK_FLAG_NONE;

        ComPtr<D3D12MA::VirtualBlock> virtualBlock;
        if (FAILED(D3D12MA::CreateVirtualBlock(&vbDesc, &virtualBlock)))
        {
            LOG_ERROR("[TransientResourcePool] CreateVirtualBlock failed.");
            bucket.m_heap.reset();
            return RHI::ResultCode::Fail;
        }
        bucket.m_offsetBlock = virtualBlock.Get();

        return RHI::ResultCode::Success;
    }

    void TransientResourcePool::ResetBucket(HeapBucket& bucket)
    {
        bucket.m_resourceCache.clear();

        if (GetDescriptor().m_allowCrossBatchReuse)
        {
            for (auto& placement : bucket.m_placements)
            {
                if (!placement.m_resource)
                {
                    continue;
                }

                ID3D12Resource* dx12Resource = nullptr;
                if (placement.m_resourceType == RHI::BarrierResourceType::Image)
                {
                    dx12Resource = static_cast<Image*>(placement.m_resource.get())->GetMemoryView().GetMemory();
                }
                else
                {
                    dx12Resource = static_cast<Buffer*>(placement.m_resource.get())->GetMemoryView().GetMemory();
                }

                if (dx12Resource)
                {
                    // intrusive_ptr 从 raw pointer 构造会 AddRef，placements.clear() 后 cache 仍持 ref
                    bucket.m_resourceCache[placement.m_cacheKey].emplace_back(dx12Resource);
                }
            }
        }

        bucket.m_deviceMemoryBarriers.clear();
        bucket.m_placements.clear();
        bucket.m_chainTails.clear();
        if (bucket.m_offsetBlock)
        {
            bucket.m_offsetBlock->Clear();
        }
        bucket.m_committedFallbacks.clear();
    }

    void TransientResourcePool::DestroyBucket(HeapBucket& bucket)
    {
        // shutdown 时不能假设 GPU idle，走 release queue 兜底 frameCountMax 帧
        auto factory = Service<ID3D12FactoryInterface>::Get();
        if (factory)
        {
            Device& device = GetDevice();
            for (auto& [key, list] : bucket.m_resourceCache)
            {
                for (auto& resource : list)
                {
                    if (resource)
                    {
                        factory->QueueForRelease(device, eastl::move(resource));
                    }
                }
            }
        }
        bucket.m_resourceCache.clear();

        bucket.m_deviceMemoryBarriers.clear();
        bucket.m_placements.clear();
        bucket.m_chainTails.clear();
        bucket.m_committedFallbacks.clear();

        // D3D12MA::VirtualBlock::~VirtualBlock asserts that all allocations have
        // been freed. Normal-frame ResetBucket handles this via Clear(), but at
        // shutdown DestroyBucket is called on every bucket and the most recent
        // frameCountMax-1 buckets haven't been cycled through ResetBucket yet.
        if (bucket.m_offsetBlock)
        {
            bucket.m_offsetBlock->Clear();
        }
        bucket.m_offsetBlock.reset();

        if (bucket.m_heap)
        {
            m_releaseQueue.QueueForCollect(eastl::move(bucket.m_heap));
        }
    }

    RHI::Image* TransientResourcePool::CreateImageInternal(
        const RHI::TransientImageCreateInfo& createInfo,
        const RHI::TransientAllocationFence& allocFence)
    {
        D3D12_RESOURCE_DESC resourceDesc;
        ConvertImageDescriptor(createInfo.m_descriptor, resourceDesc);

        Device& device = GetDevice();
        D3D12_RESOURCE_ALLOCATION_INFO allocationInfo = device.GetDX12Device()->GetResourceAllocationInfo(0, 1, &resourceDesc);

        HeapBucket& bucket = CurrentBucket();
        const size_t descHash = HashResourceDesc(resourceDesc);
        constexpr RHI::BarrierResourceType resourceType = RHI::BarrierResourceType::Image;

        uint32_t bestChainSlot = InvalidPlacementIndex;
        uint32_t index = 0;
        for (auto& tailIndex : bucket.m_chainTails)
        {
            const Placement& tail = bucket.m_placements[tailIndex];

            if (tail.m_discard.m_timelinePosition <= allocFence.m_timelinePosition &&
                tail.m_size >= allocationInfo.SizeInBytes &&
                IsAligned(tail.m_offset, allocationInfo.Alignment)
                )
            {
                // TODO: best-fit
                bestChainSlot = index;
                break;
            }
            ++index;
        }

        const uint32_t newIndex = static_cast<uint32_t>(bucket.m_placements.size());

        if (bestChainSlot != InvalidPlacementIndex)
        {
            const uint32_t prevTailIdx = bucket.m_chainTails[bestChainSlot];
            Placement& prevTail = bucket.m_placements[prevTailIdx];

            const RHI::AttachmentStage srcStage = prevTail.m_discard.m_stage;
            const RHI::AttachmentStage dstStage = allocFence.m_stage;

            Placement newPlacement;
            newPlacement.m_offset = prevTail.m_offset;
            newPlacement.m_size = allocationInfo.SizeInBytes;
            newPlacement.m_cacheKey = MakeResourceCacheKey(prevTail.m_offset, descHash);
            newPlacement.m_alloc = allocFence;
            newPlacement.m_aliasedFrom = prevTailIdx;
            newPlacement.m_discard = RHI::TransientAllocationFence(allocFence.m_pipelines, RHI::InvalidTimelinePosition);
            newPlacement.m_resourceType = resourceType;
            bucket.m_placements.push_back(eastl::move(newPlacement));

            // push_back 后 prevTail 引用可能失效，必须用 index 重新拿
            bucket.m_placements[prevTailIdx].m_aliasedTo = newIndex;
            bucket.m_chainTails[bestChainSlot] = newIndex;

            RHI::DeviceMemoryBarrier barrier;
            barrier.m_resourceBefore = prevTail.m_resource.get();
            barrier.m_typeBefore = resourceType;
            barrier.m_srcStage       = srcStage;
            barrier.m_dstStage       = dstStage;
            bucket.m_deviceMemoryBarriers[allocFence.m_timelinePosition].push_back(barrier);
        }
        else
        {
            D3D12MA::VIRTUAL_ALLOCATION_DESC vDesc;
            vDesc.Flags = D3D12MA::VIRTUAL_ALLOCATION_FLAG_NONE;
            vDesc.Size = allocationInfo.SizeInBytes;
            vDesc.Alignment = allocationInfo.Alignment;

            D3D12MA::VirtualAllocation vAlloc;
            UINT64 offset;
            HRESULT hr = bucket.m_offsetBlock->Allocate(&vDesc, &vAlloc, &offset);
            if (FAILED(hr))
            {
                if (GetDescriptor().m_allowCommittedFallback)
                {
                    LOG_WARN("[TransientResourcePool] VirtualBlock full (size {} align {}); "
                             "falling back to committed image.",
                             allocationInfo.SizeInBytes, allocationInfo.Alignment);
                    return CreateCommittedImage(createInfo, resourceDesc, allocationInfo);
                }
                LOG_ERROR("[TransientResourcePool] VirtualBlock allocation failed (size {} align {}); "
                          "committed fallback disabled by descriptor.",
                          allocationInfo.SizeInBytes, allocationInfo.Alignment);
                return nullptr;
            }

            Placement newPlacement;
            newPlacement.m_offset = offset;
            newPlacement.m_size = allocationInfo.SizeInBytes;
            newPlacement.m_cacheKey = MakeResourceCacheKey(offset, descHash);
            newPlacement.m_alloc = allocFence;
            newPlacement.m_discard = RHI::TransientAllocationFence(allocFence.m_pipelines, RHI::InvalidTimelinePosition);
            newPlacement.m_resourceType = resourceType;
            bucket.m_placements.push_back(eastl::move(newPlacement));

            bucket.m_chainTails.push_back(newIndex);
        }

        auto factory = Service<RHI::Factory>::Get();
        ASSERT(factory, "RHI::Factory is null when creating transient image.");

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

        auto& newPlacement = bucket.m_placements.back();

        Ptr<ID3D12Resource> cachedResource;
        if (GetDescriptor().m_allowCrossBatchReuse)
        {
            auto cacheIt = bucket.m_resourceCache.find(newPlacement.m_cacheKey);
            if (cacheIt != bucket.m_resourceCache.end() && !cacheIt->second.empty())
            {
                cachedResource = eastl::move(cacheIt->second.back());
                cacheIt->second.pop_back();
                if (cacheIt->second.empty())
                {
                    bucket.m_resourceCache.erase(cacheIt);
                }
            }
        }

        RHI::ResultCode result = InitResource(image.get(), [&]() -> RHI::ResultCode
        {
            Ptr<ID3D12Resource> dx12Resource;
            if (cachedResource)
            {
                dx12Resource = eastl::move(cachedResource);
            }
            else
            {
                ComPtr<ID3D12Resource> created;
                HRESULT hr = m_allocator->CreateAliasingResource(
                    bucket.m_heap.get(),
                    newPlacement.m_offset,
                    &resourceDesc,
                    initialResourceState,
                    (isOutputMergerAttachment && createInfo.m_optimizedClearValue) ? &clearValue : nullptr,
                    IID_PPV_ARGS(&created));
                if (FAILED(hr))
                {
                    LOG_ERROR("[TransientResourcePool] CreateAliasingResource failed (HRESULT 0x{:X}).",
                            static_cast<uint32_t>(hr));
                    return RHI::ResultCode::Fail;
                }
                dx12Resource = created.Get();
            }

            Image* dx12Image = static_cast<Image*>(image.get());
            SetImageDescriptor(*image, createInfo.m_descriptor);
            MemoryView memoryView(dx12Resource.get(), MemoryViewType::Image,
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
            // 回滚。VirtualBlock 那侧的 vAlloc 不显式 free，留待下一次 ResetBucket 的 Clear()。
            if (bestChainSlot != InvalidPlacementIndex)
            {
                const uint32_t prevTailIdx = bucket.m_placements.back().m_aliasedFrom;
                bucket.m_placements[prevTailIdx].m_aliasedTo = InvalidPlacementIndex;
                bucket.m_chainTails[bestChainSlot] = prevTailIdx;

                auto barrierIt = bucket.m_deviceMemoryBarriers.find(allocFence.m_timelinePosition);
                if (barrierIt != bucket.m_deviceMemoryBarriers.end() && !barrierIt->second.empty())
                {
                    barrierIt->second.pop_back();
                    if (barrierIt->second.empty())
                    {
                        bucket.m_deviceMemoryBarriers.erase(barrierIt);
                    }
                }
            }
            else
            {
                bucket.m_chainTails.pop_back();
            }
            bucket.m_placements.pop_back();

            if (GetDescriptor().m_allowCommittedFallback)
            {
                LOG_WARN("[TransientResourcePool] CreateAliasingResource (Image) failed; "
                         "falling back to committed image.");
                return CreateCommittedImage(createInfo, resourceDesc, allocationInfo);
            }
            return nullptr;
        }

        newPlacement.m_resource = image;

        if (bestChainSlot != InvalidPlacementIndex)
        {
            auto& curBarrier = bucket.m_deviceMemoryBarriers[allocFence.m_timelinePosition].back();
            curBarrier.m_resourceAfter = image.get();
            curBarrier.m_typeAfter = resourceType;
        }

        return image.get();
    }

    RHI::Buffer* TransientResourcePool::CreateBufferInternal(
        const RHI::TransientBufferCreateInfo& createInfo,
        const RHI::TransientAllocationFence&  allocFence)
    {
        D3D12_RESOURCE_DESC resourceDesc;
        ConvertBufferDescriptor(createInfo.m_descriptor, resourceDesc);

        Device& device = GetDevice();
        D3D12_RESOURCE_ALLOCATION_INFO allocationInfo = device.GetDX12Device()->GetResourceAllocationInfo(0, 1, &resourceDesc);

        HeapBucket& bucket = CurrentBucket();
        const size_t descHash = HashResourceDesc(resourceDesc);
        constexpr RHI::BarrierResourceType resourceType = RHI::BarrierResourceType::Buffer;

        uint32_t bestChainSlot = InvalidPlacementIndex;
        uint32_t index = 0;
        for (auto& tailIndex : bucket.m_chainTails)
        {
            const Placement& tail = bucket.m_placements[tailIndex];

            if (tail.m_discard.m_timelinePosition <= allocFence.m_timelinePosition &&
                tail.m_size >= allocationInfo.SizeInBytes &&
                IsAligned(tail.m_offset, allocationInfo.Alignment)
                )
            {
                // TODO: best-fit
                bestChainSlot = index;
                break;
            }
            ++index;
        }

        const uint32_t newIndex = static_cast<uint32_t>(bucket.m_placements.size());

        if (bestChainSlot != InvalidPlacementIndex)
        {
            const uint32_t prevTailIdx = bucket.m_chainTails[bestChainSlot];
            Placement& prevTail = bucket.m_placements[prevTailIdx];

            const RHI::AttachmentStage srcStage = prevTail.m_discard.m_stage;
            const RHI::AttachmentStage dstStage = allocFence.m_stage;

            Placement newPlacement;
            newPlacement.m_offset = prevTail.m_offset;
            newPlacement.m_size = allocationInfo.SizeInBytes;
            newPlacement.m_cacheKey = MakeResourceCacheKey(prevTail.m_offset, descHash);
            newPlacement.m_alloc = allocFence;
            newPlacement.m_aliasedFrom = prevTailIdx;
            newPlacement.m_discard = RHI::TransientAllocationFence(allocFence.m_pipelines, RHI::InvalidTimelinePosition);
            newPlacement.m_resourceType = resourceType;
            bucket.m_placements.push_back(eastl::move(newPlacement));

            // push_back 后 prevTail 引用可能失效，必须用 index 重新拿
            bucket.m_placements[prevTailIdx].m_aliasedTo = newIndex;
            bucket.m_chainTails[bestChainSlot] = newIndex;

            RHI::DeviceMemoryBarrier barrier;
            barrier.m_resourceBefore = prevTail.m_resource.get();
            barrier.m_typeBefore = resourceType;
            barrier.m_srcStage       = srcStage;
            barrier.m_dstStage       = dstStage;
            bucket.m_deviceMemoryBarriers[allocFence.m_timelinePosition].push_back(barrier);
        }
        else
        {
            D3D12MA::VIRTUAL_ALLOCATION_DESC vDesc;
            vDesc.Flags = D3D12MA::VIRTUAL_ALLOCATION_FLAG_NONE;
            vDesc.Size = allocationInfo.SizeInBytes;
            vDesc.Alignment = allocationInfo.Alignment;

            D3D12MA::VirtualAllocation vAlloc;
            UINT64 offset;
            HRESULT hr = bucket.m_offsetBlock->Allocate(&vDesc, &vAlloc, &offset);
            if (FAILED(hr))
            {
                if (GetDescriptor().m_allowCommittedFallback)
                {
                    LOG_WARN("[TransientResourcePool] VirtualBlock full (size {} align {}); "
                             "falling back to committed buffer.",
                             allocationInfo.SizeInBytes, allocationInfo.Alignment);
                    return CreateCommittedBuffer(createInfo, resourceDesc, allocationInfo);
                }
                LOG_ERROR("[TransientResourcePool] VirtualBlock allocation failed (size {} align {}); "
                          "committed fallback disabled by descriptor.",
                          allocationInfo.SizeInBytes, allocationInfo.Alignment);
                return nullptr;
            }

            Placement newPlacement;
            newPlacement.m_offset = offset;
            newPlacement.m_size = allocationInfo.SizeInBytes;
            newPlacement.m_cacheKey = MakeResourceCacheKey(offset, descHash);
            newPlacement.m_alloc = allocFence;
            newPlacement.m_discard = RHI::TransientAllocationFence(allocFence.m_pipelines, RHI::InvalidTimelinePosition);
            newPlacement.m_resourceType = resourceType;
            bucket.m_placements.push_back(eastl::move(newPlacement));

            bucket.m_chainTails.push_back(newIndex);
        }

        auto factory = Service<RHI::Factory>::Get();
        ASSERT(factory, "RHI::Factory is null when creating transient buffer.");

        Ptr<RHI::Buffer> buffer = factory->CreateBuffer();
        SetResourceState(*buffer, RHI::ResourceState{});

        const RHI::ResourceState resourceState = buffer->GetResourceState();
        D3D12_RESOURCE_STATES initialResourceState = ConvertBufferAttachmentState(resourceState.m_usage, resourceState.m_access);

        auto& newPlacement = bucket.m_placements.back();

        Ptr<ID3D12Resource> cachedResource;
        if (GetDescriptor().m_allowCrossBatchReuse)
        {
            auto cacheIt = bucket.m_resourceCache.find(newPlacement.m_cacheKey);
            if (cacheIt != bucket.m_resourceCache.end() && !cacheIt->second.empty())
            {
                cachedResource = eastl::move(cacheIt->second.back());
                cacheIt->second.pop_back();
                if (cacheIt->second.empty())
                {
                    bucket.m_resourceCache.erase(cacheIt);
                }
            }
        }

        RHI::ResultCode result = InitResource(buffer.get(), [&]() -> RHI::ResultCode
        {
            Ptr<ID3D12Resource> dx12Resource;
            if (cachedResource)
            {
                dx12Resource = eastl::move(cachedResource);
            }
            else
            {
                ComPtr<ID3D12Resource> created;
                HRESULT hr = m_allocator->CreateAliasingResource(
                    bucket.m_heap.get(),
                    newPlacement.m_offset,
                    &resourceDesc,
                    initialResourceState,
                    nullptr,
                    IID_PPV_ARGS(&created));
                if (FAILED(hr))
                {
                    LOG_ERROR("[TransientResourcePool] CreateAliasingResource (Buffer) failed (HRESULT 0x{:X}).",
                            static_cast<uint32_t>(hr));
                    return RHI::ResultCode::Fail;
                }
                dx12Resource = created.Get();
            }

            Buffer* dx12Buffer = static_cast<Buffer*>(buffer.get());
            SetBufferDescriptor(*buffer, createInfo.m_descriptor);
            MemoryView memoryView(dx12Resource.get(), MemoryViewType::Buffer,
                        newPlacement.m_offset,
                        newPlacement.m_size,
                        allocationInfo.Alignment);
            dx12Buffer->m_memoryView = BufferMemoryView(eastl::move(memoryView), BufferMemoryType::Shared);
            return RHI::ResultCode::Success;
        });

        if (result != RHI::ResultCode::Success)
        {
            if (bestChainSlot != InvalidPlacementIndex)
            {
                const uint32_t prevTailIdx = bucket.m_placements.back().m_aliasedFrom;
                bucket.m_placements[prevTailIdx].m_aliasedTo = InvalidPlacementIndex;
                bucket.m_chainTails[bestChainSlot] = prevTailIdx;

                auto barrierIt = bucket.m_deviceMemoryBarriers.find(allocFence.m_timelinePosition);
                if (barrierIt != bucket.m_deviceMemoryBarriers.end() && !barrierIt->second.empty())
                {
                    barrierIt->second.pop_back();
                    if (barrierIt->second.empty())
                    {
                        bucket.m_deviceMemoryBarriers.erase(barrierIt);
                    }
                }
            }
            else
            {
                bucket.m_chainTails.pop_back();
            }
            bucket.m_placements.pop_back();

            if (GetDescriptor().m_allowCommittedFallback)
            {
                LOG_WARN("[TransientResourcePool] CreateAliasingResource (Buffer) failed; "
                         "falling back to committed buffer.");
                return CreateCommittedBuffer(createInfo, resourceDesc, allocationInfo);
            }
            return nullptr;
        }

        newPlacement.m_resource = buffer;

        if (bestChainSlot != InvalidPlacementIndex)
        {
            auto& curBarrier = bucket.m_deviceMemoryBarriers[allocFence.m_timelinePosition].back();
            curBarrier.m_resourceAfter = buffer.get();
            curBarrier.m_typeAfter = resourceType;
        }

        return buffer.get();
    }

    RHI::Image* TransientResourcePool::CreateCommittedImage(
        const RHI::TransientImageCreateInfo& createInfo,
        const D3D12_RESOURCE_DESC& resourceDesc,
        const D3D12_RESOURCE_ALLOCATION_INFO& allocationInfo)
    {
        HeapBucket& bucket = CurrentBucket();

        auto factory = Service<RHI::Factory>::Get();
        ASSERT(factory, "RHI::Factory is null when creating committed transient image.");

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

        RHI::ResultCode result = InitResource(image.get(), [&]() -> RHI::ResultCode
        {
            D3D12MA::ALLOCATION_DESC allocDesc = {};
            allocDesc.Flags    = D3D12MA::ALLOCATION_FLAG_COMMITTED;
            allocDesc.HeapType = ConvertHeapType(GetDescriptor().m_heapMemoryLevel,
                                                 RHI::HostMemoryAccess::Write);

            ComPtr<D3D12MA::Allocation> allocation;
            ComPtr<ID3D12Resource>      dx12Resource;
            HRESULT hr = m_allocator->CreateResource(
                &allocDesc,
                &resourceDesc,
                initialResourceState,
                (isOutputMergerAttachment && createInfo.m_optimizedClearValue) ? &clearValue : nullptr,
                &allocation,
                IID_PPV_ARGS(&dx12Resource));
            if (FAILED(hr))
            {
                LOG_ERROR("[TransientResourcePool] Committed CreateResource (Image) failed (HRESULT 0x{:X}).",
                          static_cast<uint32_t>(hr));
                return RHI::ResultCode::Fail;
            }

            Image* dx12Image = static_cast<Image*>(image.get());
            SetImageDescriptor(*image, createInfo.m_descriptor);
            MemoryView memoryView(allocation.Get(), MemoryViewType::Image,
                                  0,
                                  allocationInfo.SizeInBytes,
                                  allocationInfo.Alignment);
            dx12Image->m_residentSizeInBytes = memoryView.GetSize();
            dx12Image->m_memoryView = eastl::move(memoryView);
            dx12Image->GenerateSubresourceLayouts();
            dx12Image->InitSubresourceAttachmentState();
            return RHI::ResultCode::Success;
        });

        if (result != RHI::ResultCode::Success)
        {
            return nullptr;
        }

        bucket.m_committedFallbacks.push_back(image);
        return image.get();
    }

    RHI::Buffer* TransientResourcePool::CreateCommittedBuffer(
        const RHI::TransientBufferCreateInfo& createInfo,
        const D3D12_RESOURCE_DESC& resourceDesc,
        const D3D12_RESOURCE_ALLOCATION_INFO& allocationInfo)
    {
        HeapBucket& bucket = CurrentBucket();

        auto factory = Service<RHI::Factory>::Get();
        ASSERT(factory, "RHI::Factory is null when creating committed transient buffer.");

        Ptr<RHI::Buffer> buffer = factory->CreateBuffer();
        SetResourceState(*buffer, RHI::ResourceState{});

        const RHI::ResourceState resourceState = buffer->GetResourceState();
        D3D12_RESOURCE_STATES initialResourceState = ConvertBufferAttachmentState(resourceState.m_usage, resourceState.m_access);

        RHI::ResultCode result = InitResource(buffer.get(), [&]() -> RHI::ResultCode
        {
            D3D12MA::ALLOCATION_DESC allocDesc = {};
            allocDesc.Flags    = D3D12MA::ALLOCATION_FLAG_COMMITTED;
            allocDesc.HeapType = ConvertHeapType(GetDescriptor().m_heapMemoryLevel,
                                                 RHI::HostMemoryAccess::Write);

            ComPtr<D3D12MA::Allocation> allocation;
            ComPtr<ID3D12Resource>      dx12Resource;
            HRESULT hr = m_allocator->CreateResource(
                &allocDesc,
                &resourceDesc,
                initialResourceState,
                nullptr,
                &allocation,
                IID_PPV_ARGS(&dx12Resource));
            if (FAILED(hr))
            {
                LOG_ERROR("[TransientResourcePool] Committed CreateResource (Buffer) failed (HRESULT 0x{:X}).",
                          static_cast<uint32_t>(hr));
                return RHI::ResultCode::Fail;
            }

            Buffer* dx12Buffer = static_cast<Buffer*>(buffer.get());
            SetBufferDescriptor(*buffer, createInfo.m_descriptor);
            MemoryView memoryView(allocation.Get(), MemoryViewType::Buffer,
                                  0,
                                  allocationInfo.SizeInBytes,
                                  allocationInfo.Alignment);
            dx12Buffer->m_memoryView = BufferMemoryView(eastl::move(memoryView), BufferMemoryType::Unique);
            return RHI::ResultCode::Success;
        });

        if (result != RHI::ResultCode::Success)
        {
            return nullptr;
        }

        bucket.m_committedFallbacks.push_back(buffer);
        return buffer.get();
    }

    void TransientResourcePool::GetDeviceMemoryBarriersInternal(uint32_t timelinePosition, eastl::vector<RHI::DeviceMemoryBarrier>& out) const
    {
        const HeapBucket& bucket = CurrentBucket();
        auto it = bucket.m_deviceMemoryBarriers.find(timelinePosition);
        if (it != bucket.m_deviceMemoryBarriers.end())
        {
            out = it->second;
            return;
        }
        out = {};
    }

    void TransientResourcePool::DiscardInternal(RHI::Image* image, const RHI::TransientAllocationFence& discardFence)
    {
        HeapBucket& bucket = CurrentBucket();
        for(auto tailIndex: bucket.m_chainTails)
        {
            Placement& tail = bucket.m_placements[tailIndex];
            if (tail.m_resource.get() == image)
            {
                ASSERT(tail.m_discard.m_timelinePosition == RHI::InvalidTimelinePosition,
                    "[TransientResourcePool] Repeat discard of image %s. "
                    "Each transient resource must be discarded exactly once.",
                    image->GetName().GetCStr());

                tail.m_discard = discardFence;
                return;
            }
        }

        // committed fallback 不参与 aliasing，没 offset 要回收，no-op。
        for (auto& res : bucket.m_committedFallbacks)
        {
            if (res.get() == image)
            {
                return;
            }
        }

        ASSERT(false, "[TransientResourcePool] Discard called on image %s that is not a chain tail. "
                       "Discard must follow LIFO order on each alias chain: the last-allocated resource "
                       "at an offset must be discarded before its predecessor can be discarded.",
               image->GetName().GetCStr());
    }

    void TransientResourcePool::DiscardInternal(RHI::Buffer* buffer, const RHI::TransientAllocationFence& discardFence)
    {
        HeapBucket& bucket = CurrentBucket();
        for (auto tailIndex : bucket.m_chainTails)
        {
            Placement& tail = bucket.m_placements[tailIndex];
            if (tail.m_resource.get() == buffer)
            {
                ASSERT(tail.m_discard.m_timelinePosition == RHI::InvalidTimelinePosition,
                    "[TransientResourcePool] Repeat discard of buffer %s. "
                    "Each transient resource must be discarded exactly once.",
                    buffer->GetName().GetCStr());

                tail.m_discard = discardFence;
                return;
            }
        }

        for (auto& res : bucket.m_committedFallbacks)
        {
            if (res.get() == buffer)
            {
                return;
            }
        }

        ASSERT(false, "[TransientResourcePool] Discard called on buffer %s that is not a chain tail. "
                       "Discard must follow LIFO order on each alias chain: the last-allocated resource "
                       "at an offset must be discarded before its predecessor can be discarded.",
               buffer->GetName().GetCStr());
    }

    void TransientResourcePool::OnFrameBeginInternal()
    {
        // 推进到下一槽。frames-in-flight fence 保证此槽上一次 GPU 工作已完成。
        m_currentSlot = (m_currentSlot + 1) % static_cast<uint32_t>(m_buckets.size());
        ResetBucket(m_buckets[m_currentSlot]);
    }

    void TransientResourcePool::OnFrameEndInternal()
    {

    }

    void TransientResourcePool::ShutdownInternal()
    {
        for (auto& bucket : m_buckets)
        {
            DestroyBucket(bucket);
        }
        m_buckets.clear();

        m_releaseQueue.Shutdown();
        m_allocator.reset();
    }
}
