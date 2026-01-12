/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "BufferPool.h"

#include <mutex>
#include <Math/Bit.h>
#include <RHILimits.h>
#include "Buffer.h"

#include "../../Device/Device.h"
#include "../../Memory.h"
#include "../../MemoryView.h"
#include "../../Conversions.h"

namespace Spark::RHI::DX12
{
    class BufferPoolResolver: public ResourcePoolResolver
    {
    public:
        BufferPoolResolver(Device& device, const RHI::BufferPoolDescriptor& descriptor)
        {
            m_device = &device;

            if(CheckBitsAny(descriptor.m_bindFlags, RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::DynamicInputAssembly))
            {
                m_readOnlyState |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
            }
            if (CheckBitsAll(descriptor.m_bindFlags, RHI::BufferBindFlags::Constant))
            {
                m_readOnlyState |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            }
            if (CheckBitsAll(descriptor.m_bindFlags, RHI::BufferBindFlags::ShaderRead))
            {
                m_readOnlyState |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            }
            if (CheckBitsAll(descriptor.m_bindFlags, RHI::BufferBindFlags::Indirect))
            {
                m_readOnlyState |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
            }
        }

        CpuVirtualAddress MapBuffer(const RHI::BufferMapRequest& request)
        {
            MemoryView stagingMemory = m_device->AcquireStagingMemory(request.m_byteCount, Alignment::Buffer);
            if (!stagingMemory.IsValid())
            {
                return nullptr;
            }

            BufferUploadPacket uploadRequest;
            
            // Fill the packet with the source and destination regions for copy.
            Buffer* buffer = static_cast<Buffer*>(request.m_buffer);
            buffer->m_pendingResolves++;

            uploadRequest.m_buffer = buffer;
            uploadRequest.m_memory = buffer->GetMemoryView().GetMemory();
            uploadRequest.m_memoryByteOffset = buffer->GetMemoryView().GetOffset() + request.m_byteOffset;
            uploadRequest.m_sourceMemory = eastl::move(stagingMemory);

            CpuVirtualAddress address = uploadRequest.m_sourceMemory.Map(RHI::HostMemoryAccess::Write);

            // Once the uploadRequest has been processed, add it to the uploadPackets queue.
            m_uploadPacketsLock.lock();
            m_uploadPackets.emplace_back(eastl::move(uploadRequest));
            m_uploadPacketsLock.unlock();

            return address;
        }

        /*
        void Compile(Scope& scope) override
        {
            for (BufferUploadPacket& packet : m_uploadPackets)
            {
                packet.m_sourceMemory.Unmap(RHI::HostMemoryAccess::Write);

                if (packet.m_buffer->IsAttachment())
                {
                    // Informs the graph compiler that this buffer is in the copy destination state.
                    packet.m_buffer->m_initialAttachmentState = D3D12_RESOURCE_STATE_COPY_DEST;
                }
                else
                {
                    // Tracks the union of non-attachment buffers which are transitioned manually.
                    m_nonAttachmentBufferUnion.emplace(packet.m_memory);
                }
            }
        }
        

        void Resolve(CommandList& commandList) const override
        {
            for (const BufferUploadPacket& packet : m_uploadPackets)
            {
                commandList.GetCommandList()->CopyBufferRegion(
                    packet.m_memory,
                    packet.m_memoryByteOffset,
                    packet.m_sourceMemory.GetMemory(),
                    packet.m_sourceMemory.GetOffset(),
                    packet.m_sourceMemory.GetSize());
            }
        }

        void QueueEpilogueTransitionBarriers(CommandList& commandList) const override
        {
            for (ID3D12Resource* resource : m_nonAttachmentBufferUnion)
            {
                commandList.QueueTransitionBarrier(resource, D3D12_RESOURCE_STATE_COPY_DEST, m_readOnlyState);
            }
        }

        void Deactivate() override
        {
            eastl::for_each(m_uploadPackets.begin(), m_uploadPackets.end(), [](auto& packet)
            {
                AZ_Assert(packet.m_buffer->m_pendingResolves, "There's no pending resolves for buffer %s", packet.m_buffer->GetName().GetCStr());
                packet.m_buffer->m_pendingResolves--;
            });

            m_uploadPackets.clear();
            m_nonAttachmentBufferUnion.clear();
        }

        void OnResourceShutdown(const RHI::Resource& resource) override
        {
            const Buffer& buffer = static_cast<const Buffer&>(resource);
            if (!buffer.m_pendingResolves)
            {
                return;
            }

            AZStd::lock_guard<AZStd::mutex> lock(m_uploadPacketsLock);
            auto eraseBeginIt = std::stable_partition(
                m_uploadPackets.begin(),
                m_uploadPackets.end(),
                [&buffer](const BufferUploadPacket& packet)
                {
                    return packet.m_buffer != &buffer;
                }
            );

            for (auto it = eraseBeginIt; it != m_uploadPackets.end(); ++it)
            {
                it->m_sourceMemory.Unmap(RHI::HostMemoryAccess::Write);
            }
            m_uploadPackets.resize(AZStd::distance(m_uploadPackets.begin(), eraseBeginIt));
            m_nonAttachmentBufferUnion.erase(buffer.GetMemoryView().GetMemory());
        }
        */

    private:
        struct BufferUploadPacket
        {
            Buffer* m_buffer = nullptr;

            // Buffer properties are held directly to avoid an indirection through Buffer in the inner loops.
            Memory* m_memory = nullptr;
            size_t m_memoryByteOffset = 0;

            MemoryView m_sourceMemory;
        };

        Device* m_device = nullptr;
        D3D12_RESOURCE_STATES m_readOnlyState = D3D12_RESOURCE_STATE_COMMON;
        std::mutex m_uploadPacketsLock;
        eastl::vector<BufferUploadPacket> m_uploadPackets;
        eastl::unordered_set<Memory*> m_nonAttachmentBufferUnion;
    };

    Device& BufferPool::GetDevice() const
    {
        return static_cast<Device&>(Base::GetDevice());
    }

    void BufferPool::OnFrameEnd()
    {
        //
        Base::OnFrameEnd();
    }

    RHI::ResultCode BufferPool::InitInternal(RHI::Device& deviceBase, const RHI::BufferPoolDescriptor& descriptorBase)
    {
        Device& device = static_cast<Device&>(deviceBase);

        uint32_t bufferPageSize = RHI::DefaultValues::Memory::BufferPoolPageSizeInBytes;

        if (descriptorBase.m_largestPooledAllocationSizeInBytes > 0)
        {
            bufferPageSize = eastl::max<uint32_t>(bufferPageSize, static_cast<uint32_t>(descriptorBase.m_largestPooledAllocationSizeInBytes));
        }

        D3D12MA::ALLOCATOR_DESC desc = {};
        desc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;
        desc.pDevice = device.GetDevice();
        desc.pAdapter = device.GetPhysicalDevice().GetAdapter();
        desc.PreferredBlockSize = bufferPageSize;

        D3D12MA::Allocator* pAllocator;
        if (FAILED(D3D12MA::CreateAllocator(&desc, &pAllocator)))
        {
            LOG_ERROR("[BufferPool] Failed to initialize the D3D12MemoryAllocator.");
            return RHI::ResultCode::Fail;
        }
        m_allocator = pAllocator;

        if (descriptorBase.m_heapMemoryLevel == RHI::HeapMemoryLevel::Device)
        {
            SetResolver(eastl::make_unique<BufferPoolResolver>(device, descriptorBase));
        }

        D3D12MAReleaseQueue::Descriptor releaseQueueDescriptor;
        releaseQueueDescriptor.m_collectLatency = device.GetDescriptor().m_frameCountMax;
        m_releaseQueue.Init(releaseQueueDescriptor);

        return RHI::ResultCode::Success;
    }

    void BufferPool::ShutdownInternal()
    {
        m_allocator.reset();
    }

    RHI::ResultCode BufferPool::InitBufferInternal(RHI::Buffer& bufferBase, const RHI::BufferDescriptor& bufferDescriptor)
    {
        D3D12_RESOURCE_DESC resourceDesc;
        ConvertBufferDescriptor(bufferDescriptor, resourceDesc);

        D3D12MA::ALLOCATION_DESC allocDesc = {};
        allocDesc.HeapType = ConvertHeapType(GetDescriptor().m_heapMemoryLevel, GetDescriptor().m_hostMemoryAccess);
        allocDesc.Flags = D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_STRATEGY_BEST_FIT;

        D3D12_RESOURCE_STATES initialResourceState = ConvertInitialResourceState(GetDescriptor().m_heapMemoryLevel, GetDescriptor().m_hostMemoryAccess);
        if (CheckBitsAny(bufferDescriptor.m_bindFlags, RHI::BufferBindFlags::RayTracingAccelerationStructure))
        {
            initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        }

        D3D12MA::Allocation* allocation = nullptr;
        HRESULT result = m_allocator->CreateResource(
            &allocDesc,
            &resourceDesc,
            initialResourceState,
            NULL,
            &allocation,
            IID_NULL,
            NULL
        );

        if (FAILED(result))
        {
            LOG_ERROR("[BufferPool] D3D12MA Create buffer resource failed!");
            return RHI::ResultCode::Fail;
        }

        // 创建一个默认BufferMemoryView，使用全部Memory(ID3DResource)
        MemoryView memoryView(allocation, MemoryViewType::Buffer, 0, bufferDescriptor.m_byteCount, bufferDescriptor.m_alignment);
        BufferMemoryView bufferMemoryView(eastl::move(memoryView), allocation->GetHeap() ? BufferMemoryType::Shared : BufferMemoryType::Unique);
        Buffer& buffer = static_cast<Buffer&>(bufferBase);
        buffer.m_memoryView = eastl::move(bufferMemoryView);
        buffer.m_initialAttachmentState = initialResourceState;
        return RHI::ResultCode::Success;
    }

    void BufferPool::ShutdownResourceInternal(RHI::Resource& resourceBase)
    {
        if (auto* resolver = GetResolver())
        {
            resolver->OnResourceShutdown(resourceBase);
        }

        Buffer& buffer = static_cast<Buffer&>(resourceBase);
        m_releaseQueue.Collect(buffer.GetMemoryView().GetMemoryAllocation());
        // 这里发送移动赋值，原MemoryView持有的MemoryAllocation自动release
        buffer.m_memoryView = {};
        buffer.m_initialAttachmentState = D3D12_RESOURCE_STATE_COMMON;
        buffer.m_pendingResolves = 0;
    }

    RHI::ResultCode BufferPool::OrphanBufferInternal(RHI::Buffer& bufferBase)
    {
        // [TODO]
        return RHI::ResultCode::InvalidOperation;
    }

    RHI::ResultCode BufferPool::MapBufferInternal(const RHI::BufferMapRequest& request, RHI::BufferMapResponse& response)
    {
        const RHI::BufferPoolDescriptor& poolDescriptor = GetDescriptor();
        Buffer& buffer = *static_cast<Buffer*>(request.m_buffer);
        CpuVirtualAddress mappedData = nullptr;

        if (poolDescriptor.m_heapMemoryLevel == RHI::HeapMemoryLevel::Host)
        {
            mappedData = buffer.GetMemoryView().Map(poolDescriptor.m_hostMemoryAccess);

            if (!mappedData)
            {
                return RHI::ResultCode::Fail;
            }
            mappedData += request.m_byteOffset;
        }
        else
        {
            mappedData = GetResolver()->MapBuffer(request);
            if (!mappedData)
            {
                return RHI::ResultCode::OutOfMemory;
            }
        }

        response.m_data = mappedData;
        return RHI::ResultCode::Success;
    }

    void BufferPool::UnmapBufferInternal(RHI::Buffer& bufferBase)
    {
        const RHI::BufferPoolDescriptor& poolDescriptor = GetDescriptor();
        Buffer& buffer = static_cast<Buffer&>(bufferBase);

        if (poolDescriptor.m_heapMemoryLevel == RHI::HeapMemoryLevel::Host)
        {
            buffer.GetMemoryView().Unmap(poolDescriptor.m_hostMemoryAccess);
        }
    }

    RHI::ResultCode StreamBufferInternal(const RHI::BufferStreamRequest& request)
    {
        // [TODO]
        return RHI::ResultCode::InvalidOperation;
    }
}