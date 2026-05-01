#pragma once

#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>
#include <EASTL/numeric_limits.h>

#include <RHI/Device/DeviceObjectFactory.h>
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

        void DiscardInternal(RHI::Image* image, const RHI::TransientAllocationFence& discardFence) override;

        void GetAliasingBarriersInternal(
            uint32_t timelinePosition,
            RHI::AliasingBarrierList& out) const override;

        void OnFrameBeginInternal() override;

        void OnFrameEndInternal() override;

        // CreateBufferInternal / DiscardInternal
        // are added in subsequent stages.
        ////////////////////////////////////////////////////////////////////////

        static constexpr uint32_t InvalidPlacementIndex = eastl::numeric_limits<uint32_t>::max();

        struct Placement
        {
            Ptr<RHI::Resource>            m_resource    = nullptr;
            uint64_t                      m_offset      = 0;
            uint64_t                      m_size        = 0;
            RHI::TransientAllocationFence m_alloc;
            RHI::TransientAllocationFence m_discard;
            // 别名关系：本 Placement 复用了 m_aliasedFrom 的 offset 并被 m_aliasedTo 复用
            // 记录其在 HeapBucket::m_placements 中的索引
            uint32_t                      m_aliasedFrom = InvalidPlacementIndex;
            uint32_t                      m_aliasedTo = InvalidPlacementIndex;
        };

        struct HeapBucket
        {
            Ptr<D3D12MA::Allocation>   m_heap;
            Ptr<D3D12MA::VirtualBlock> m_offsetBlock;
            eastl::vector<Placement>   m_placements;

            // 当前每条链的链尾索引。一个 offset 一条链 = 一个 entry。
            // Create 时：要么 push_back 新 entry（开新链），要么覆盖某 entry（接到现有链尾后）
            eastl::vector<uint32_t>    m_chainTails;
        };

        /////////////////////////////////////////////
        // [TODO] 跨帧缓存ID3D12Resource
        struct ImageCacheEntry
        {
            Ptr<ID3D12Resource>  m_resource;
            RHI::ImageDescriptor m_descriptor; 
            uint64_t             m_offset;

            size_t GetHash() const;
        };

        struct BufferCacheEntry
        {
            Ptr<ID3D12Resource>   m_resource;
            RHI::BufferDescriptor m_descriptor; 
            uint64_t              m_offset;

            size_t GetHash() const;
        };
        /////////////////////////////////////////////

        Ptr<D3D12MA::Allocator>  m_allocator;
        HeapBucket               m_bucket;

        D3D12MAReleaseQueue      m_releaseQueue;

        eastl::unordered_map<uint32_t, eastl::vector<RHI::TransientAliasingBarrier>> m_aliasingBarriers;

        // 跨批次 placed resource 复用：descriptor 哈希 → 候选资源列表
        // [TODO] 跨帧缓存ID3D12Resource
        eastl::unordered_map<uint64_t, eastl::vector<Ptr<ID3D12Resource>>> m_placedResourceCache;
    };
}
