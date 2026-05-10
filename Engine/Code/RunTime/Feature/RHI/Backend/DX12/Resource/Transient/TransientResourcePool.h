#pragma once

#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>
#include <EASTL/numeric_limits.h>

#include <RHI/Device/DeviceObjectFactory.h>
#include <RHI/Resource/ResourceState.h>
#include <RHI/Resource/Transient/TransientResourcePool.h>

#include <DX12.h>
#include <ReleaseQueue.h>
#include <3rdParty/D3D12MA/D3D12MemAlloc.h>

namespace Spark::RHI
{
    class Resource;
}

namespace Spark::RHI::DX12
{
    class Device;

    class TransientResourcePool final : public RHI::TransientResourcePool
    {
    public:
        Device& GetDevice() const;

    private:
        TransientResourcePool() = default;

        friend class DeviceObjectFactory<TransientResourcePool>;

        ////////////////////////////////////////////////////////////////////////
        // RHI::TransientResourcePool
        RHI::ResultCode InitInternal(
            RHI::Device& device,
            const RHI::TransientResourcePoolDescriptor& descriptor) override;

        void ShutdownInternal() override;

        RHI::Image* CreateImageInternal(
            const RHI::TransientImageCreateInfo& createInfo,
            const RHI::TransientAllocationFence& allocFence) override;

        RHI::Buffer* CreateBufferInternal(
            const RHI::TransientBufferCreateInfo& createInfo,
            const RHI::TransientAllocationFence&  allocFence) override;

        void DiscardInternal(RHI::Image* image, const RHI::TransientAllocationFence& discardFence) override;

        void DiscardInternal(RHI::Buffer* buffer, const RHI::TransientAllocationFence& discardFence) override;

        void GetAliasingBarriersInternal(
            uint32_t timelinePosition,
            eastl::vector<RHI::AliasingBarrier>& out) const override;

        void OnFrameBeginInternal() override;

        void OnFrameEndInternal() override;
        ////////////////////////////////////////////////////////////////////////

        static constexpr uint32_t InvalidPlacementIndex = eastl::numeric_limits<uint32_t>::max();

        struct Placement
        {
            Ptr<RHI::Resource>            m_resource    = nullptr;
            uint64_t                      m_offset      = 0;
            uint64_t                      m_size        = 0;
            uint64_t                      m_cacheKey    = 0;
            RHI::TransientAllocationFence m_alloc;
            RHI::TransientAllocationFence m_discard;
            // m_aliasedFrom/To 互指 HeapBucket::m_placements 中的索引，构成 alias 链
            uint32_t                      m_aliasedFrom = InvalidPlacementIndex;
            uint32_t                      m_aliasedTo = InvalidPlacementIndex;
            // harvest 时据此把 m_resource cast 回 Image / Buffer 取底层 ID3D12Resource
            RHI::BarrierResourceType      m_resourceType = RHI::BarrierResourceType::Image;
        };

        // 一帧一个 bucket。引擎 frames-in-flight fence 保证轮回到目标槽时它上一次的
        // GPU 消费已完成。AliasingBarrier 也按槽存，避免跨帧 timeline position 撞 key。
        struct HeapBucket
        {
            Ptr<D3D12MA::Allocation>   m_heap;
            Ptr<D3D12MA::VirtualBlock> m_offsetBlock;
            eastl::vector<Placement>   m_placements;

            // 每条 alias 链的链尾索引；一条链 = 一个 offset
            eastl::vector<uint32_t>    m_chainTails;

            eastl::unordered_map<uint32_t, eastl::vector<RHI::AliasingBarrier>> m_aliasingBarriers;

            // 跨槽轮回复用 ID3D12Resource：harvest 时按 (offset, descHash) 入 cache，
            // 下次轮到本槽时 Create*Internal 命中即可跳过 CreateAliasingResource。
            // value 是 vector 因为单帧内同 (offset, desc) 可能出现多次（同链多段同描述符）。
            eastl::unordered_map<uint64_t, eastl::vector<Ptr<ID3D12Resource>>> m_resourceCache;

            // VirtualBlock 满或 CreateAliasingResource 失败时的兜底：自有 D3D12MA::Allocation，
            // 不进 alias 链，随槽轮回释放。
            eastl::vector<Ptr<RHI::Resource>> m_committedFallbacks;
        };

        RHI::ResultCode InitBucket(HeapBucket& bucket,
                                   const RHI::TransientResourcePoolDescriptor& descriptor,
                                   uint64_t heapSize,
                                   uint64_t heapAlign);
        void ResetBucket(HeapBucket& bucket);
        void DestroyBucket(HeapBucket& bucket);

        // 调用方已确认 m_allowCommittedFallback=true
        RHI::Image*  CreateCommittedImage(
            const RHI::TransientImageCreateInfo& createInfo,
            const D3D12_RESOURCE_DESC& resourceDesc,
            const D3D12_RESOURCE_ALLOCATION_INFO& allocationInfo);
        RHI::Buffer* CreateCommittedBuffer(
            const RHI::TransientBufferCreateInfo& createInfo,
            const D3D12_RESOURCE_DESC& resourceDesc,
            const D3D12_RESOURCE_ALLOCATION_INFO& allocationInfo);

        HeapBucket&       CurrentBucket()       { return m_buckets[m_currentSlot]; }
        const HeapBucket& CurrentBucket() const { return m_buckets[m_currentSlot]; }

        Ptr<D3D12MA::Allocator>   m_allocator;
        eastl::vector<HeapBucket> m_buckets;
        uint32_t                  m_currentSlot = 0;

        D3D12MAReleaseQueue       m_releaseQueue;
    };
}
