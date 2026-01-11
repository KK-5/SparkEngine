#include "DescriptorFactory.h"

#include <Math/Bit.h>
#include <LOG/SpdLogSystem.h>

namespace Spark::RHI::DX12
{
    void DescriptorHandleFactory::Init(const Descriptor& descriptor)
    {
        m_descriptor = descriptor;

        m_stride = m_descriptor.device->GetDescriptorHandleIncrementSize(m_descriptor.type);
        m_cpuStart = m_descriptor.descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        m_cpuStart.ptr += m_descriptor.heapOffset * m_stride;
        m_gpuStart = {};

        const bool isGpuVisible = CheckBitsAll(m_descriptor.flags, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
        if (isGpuVisible)
        {
            LOG_INFO("[DescriptorHandleFactory] Creating a shader visible descriptor heap. Recommanded use DescriptorTableFactory.");
            m_gpuStart = m_descriptor.descriptorHeap->GetGPUDescriptorHandleForHeapStart();
            m_gpuStart.ptr += m_descriptor.heapOffset * m_stride;
        }

        m_handlePool.resize(m_descriptor.descriptorCount);
        m_freeList.resize(m_descriptor.descriptorCount);
        for (uint32_t i = 0; i < m_descriptor.descriptorCount; ++i)
        {
            m_handlePool[i] = DescriptorHandle(m_descriptor.type, m_descriptor.flags, i);
            // 从m_freeList.back()处开始取用，这里希望先取索引小的，所以倒序初始化它
            m_freeList[i] = m_descriptor.descriptorCount - i - 1;
        }
    }

    void DescriptorHandleFactory::Shutdown()
    {
        m_freeList.clear();
        m_handlePool.clear();
    }

    DescriptorHandle* DescriptorHandleFactory::Allocate()
    {
        if (m_freeList.empty())
        {
            LOG_ERROR("[DescriptorHandleFactory] ID3D12DescriptorHeap does not have enough handle.");
            return nullptr;
        }

        uint32_t index = m_freeList.back();
        m_freeList.pop_back();
        return &m_handlePool[index];
    }

    void DescriptorHandleFactory::DeAllocate(DescriptorHandle* handle, bool isPoolShutdown)
    {
        if (isPoolShutdown)
        {
            return;
        }

        m_freeList.push_back(handle->m_index);
    }

    bool RecycleObject([[maybe_unused]]DescriptorHandle* handle)
    {
        // DescriptorHandle任何情况下都不需要ObjectPool复用它
        return false;
    }

    const DescriptorHandleFactory::Descriptor& DescriptorHandleFactory::GetDescriptor() const
    {
        return m_descriptor;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHandleFactory::GetD3D12CPUDescriptorHandle(const DescriptorHandle& handle) const
    {
        return D3D12_CPU_DESCRIPTOR_HANDLE{ m_cpuStart.ptr + handle.m_index * m_stride };
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHandleFactory::GetD3D12GPUDescriptorHandle(const DescriptorHandle& handle) const
    {
        return D3D12_GPU_DESCRIPTOR_HANDLE{ m_gpuStart.ptr + handle.m_index * m_stride };
    }

    uint32_t DescriptorHandleFactory::GetDescriptorHandleIncrementSize() const
    {
        return m_stride;
    }


    ////////////////////////////////////////////////////
    DescriptorTableFactory::NodeIndex DescriptorTableFactory::CreateNode()
    {
        if (!m_freeList.empty())
        {
            NodeIndex index = m_freeList.back();
            m_freeList.pop_back();
            return index;
        }
        else
        {
            m_nodes.emplace_back();
            return m_nodes.size() - 1;
        }
    }

    void DescriptorTableFactory::ReleaseNode(NodeIndex index)
    {
        m_freeList.push_back(index);
    }

    DescriptorTableFactory::NodeIndex DescriptorTableFactory::InsertNode(NodeIndex prevIndex, size_t offset, size_t size)
    {
        NodeIndex index = CreateNode();

        Node& curNode = m_nodes[index];
        curNode.offset = offset;
        curNode.size = size;

        Node& prevNode = m_nodes[prevIndex];
        curNode.nextFree = prevNode.nextFree;
        prevNode.nextFree = index;

        return index;
    }

    void DescriptorTableFactory::RemoveNode(NodeIndex prevIndex, NodeIndex curIndex)
    {
        Node& prevNode = m_nodes[prevIndex];
        Node& curNode = m_nodes[curIndex];
        prevNode.nextFree = curNode.nextFree;
        curNode.nextFree = -1;
        ReleaseNode(curIndex);
    }

    void DescriptorTableFactory::FindNode(const FindNodeRequest& request, FindNodeResponse& response)
    {
        size_t remainingSize = static_cast<size_t>(-1);

        NodeIndex prevIndex = m_headIndex;
        NodeIndex curIndex = m_nodes[prevIndex].nextFree;

        while (curIndex != -1)
        {
            Node& node = m_nodes[curIndex];

            if (request.size <= node.size)
            {
                size_t curRemainingSize = node.size - request.size;
                if (curRemainingSize < remainingSize)
                {
                    remainingSize = curRemainingSize;
                    response.remainingSize = curRemainingSize;
                    response.prevIndex = prevIndex;
                    response.nodeIndex = curIndex;
                }
            }

            prevIndex = curIndex;
            curIndex = node.nextFree;
        }
    }

    void DescriptorTableFactory::FreeInternal(const DescriptorTable& table)
    {
        const size_t offset = table.GetOffset().m_index;
        const size_t size = table.GetSize();

        NodeIndex prevIndex = m_headIndex;
        NodeIndex curIndex = m_nodes[prevIndex].nextFree;
        NodeIndex nextIndex;

        while (curIndex != -1)
        {
            Node& node = m_nodes[curIndex];
            if (node.offset > offset)
            {
                nextIndex = curIndex;
                break;
            }

            prevIndex = curIndex;
            curIndex = node.nextFree;
        }

        bool previousMerged = false;

        // Try to merge with previous node
        if (prevIndex != m_headIndex)
        {
            Node& prevNode = m_nodes[prevIndex];
            if (prevNode.offset + prevNode.size == offset)
            {
                prevNode.size += size;
                previousMerged = true;
            }
        }

        if (!previousMerged)
        {
            prevIndex = InsertNode(prevIndex, offset, size);
        }

        // Try to merge with next node
        if (nextIndex != -1)
        {
            Node& nextNode = m_nodes[nextIndex];
            Node& curNode = m_nodes[prevIndex];
            ASSERT(curNode.offset < nextNode.offset, "[DescriptorTableFactory] Node order error when free descriptor table.");
            if (curNode.offset + curNode.size == nextNode.offset)
            {
                curNode.size += nextNode.size;
                RemoveNode(prevIndex, nextIndex);
            }
        }
    }

    void DescriptorTableFactory::Init(const Descriptor& descriptor)
    {
        m_descriptor = descriptor;

        const bool isGpuVisible = CheckBitsAll(m_descriptor.flags, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
        if (!isGpuVisible)
        {
            LOG_INFO("[DescriptorTableFactory] DescriptorTableFactory should be created with shader visible flag.");
        }

        m_stride = m_descriptor.device->GetDescriptorHandleIncrementSize(m_descriptor.type);
        m_cpuStart = m_descriptor.descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        m_cpuStart.ptr += m_descriptor.heapOffset * m_stride;
        m_gpuStart = m_descriptor.descriptorHeap->GetGPUDescriptorHandleForHeapStart();
        m_gpuStart.ptr += m_descriptor.heapOffset * m_stride;

        m_headIndex = CreateNode();
        InsertNode(m_headIndex, 0, m_descriptor.descriptorCount);
    }

    void DescriptorTableFactory::Shutdown()
    {
        m_freeList.clear();
        m_nodes.clear();
    }

    DescriptorTable* DescriptorTableFactory::Allocate(uint32_t count)
    {
        ASSERT(count > 0, "[DescriptorTableFactory] Allocate DescriptorTable with count 0.");

        FindNodeRequest request;
        request.size = count;

        FindNodeResponse response;
        FindNode(request, response);

        if (response.nodeIndex == -1)
        {
            LOG_ERROR("[DescriptorTableFactory] ID3D12DescriptorHeap does not have enough handle.");
            return nullptr;
        }

        Node& foundNode = m_nodes[response.nodeIndex];
        DescriptorHandle handle(
            m_descriptor.type,
            m_descriptor.flags,
            static_cast<uint32_t>(foundNode.offset));
        
        m_tablePool.emplace(foundNode.offset, DescriptorTable(handle, count));

        if (response.remainingSize > 0)
        {
            InsertNode(response.prevIndex, foundNode.offset + count, response.remainingSize);
        }
        RemoveNode(response.prevIndex, response.nodeIndex);

        return &m_tablePool[foundNode.offset];
    }

    void DescriptorTableFactory::DeAllocate(DescriptorTable* table, bool isPoolShutdown)
    {
        if (isPoolShutdown || table == nullptr)
        {
            return;
        }

        size_t offset = table->GetOffset().m_index;
        auto it = m_tablePool.find(offset);
        if (it == m_tablePool.end())
        {
            LOG_ERROR("[DescriptorTableFactory] Trying to deallocate a DescriptorTable that is not allocated from this factory.");
            return;
        }

        FreeInternal(it->second);

        m_tablePool.erase(it);
    }

    bool RecycleObject(DescriptorTable* table)
    {
        // DescriptorTable任何情况下都不需要ObjectPool复用它
        return false;
    }

    const DescriptorTableFactory::Descriptor& DescriptorTableFactory::GetDescriptor() const
    {
        return m_descriptor;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorTableFactory::GetD3D12CPUDescriptorHandle(const DescriptorTable& table) const
    {
        return D3D12_CPU_DESCRIPTOR_HANDLE{ m_cpuStart.ptr + table.GetOffset().m_index * m_stride };
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorTableFactory::GetD3D12GPUDescriptorHandle(const DescriptorTable& table) const
    {
        return D3D12_GPU_DESCRIPTOR_HANDLE{ m_gpuStart.ptr + table.GetOffset().m_index * m_stride };
    }

    uint32_t DescriptorTableFactory::GetDescriptorHandleIncrementSize() const
    {
        return m_stride;
    }
}