#pragma once

#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <Object/IObjectFactory.h>

#include <DX12.h>
#include "Descriptor.h"

namespace Spark::RHI::DX12
{
    class DescriptorHandleFactory final : IObjectFactory<DescriptorHandle>
    {
    public:
        struct Descriptor
        {
            ID3D12DeviceX* device;
            ID3D12DescriptorHeap* descriptorHeap;
            D3D12_DESCRIPTOR_HEAP_TYPE type;
            D3D12_DESCRIPTOR_HEAP_FLAGS flags;
            uint32_t heapOffset;
            uint32_t descriptorCount;
        };

        void Init(const Descriptor& descriptor);

        void Shutdown();

        DescriptorHandle* Allocate();

        void DeAllocate(DescriptorHandle* handle, bool isPoolShutdown);

        bool RecycleObject(DescriptorHandle* handle);

        const Descriptor& GetDescriptor() const;

        D3D12_CPU_DESCRIPTOR_HANDLE GetD3D12CPUDescriptorHandle(const DescriptorHandle& handle) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetD3D12GPUDescriptorHandle(const DescriptorHandle& handle) const;

        uint32_t GetDescriptorHandleIncrementSize() const;

    private:
        Descriptor m_descriptor;

        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart = {};
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart = {};
        uint32_t m_stride = 0;
        eastl::vector<DescriptorHandle> m_handlePool;
        eastl::vector<uint32_t> m_freeList;
    };

    class DescriptorTableFactory final : IObjectFactory<DescriptorTable>
    {
    public:
        struct Descriptor
        {
            ID3D12DeviceX* device;
            ID3D12DescriptorHeap* descriptorHeap;
            D3D12_DESCRIPTOR_HEAP_TYPE type;
            D3D12_DESCRIPTOR_HEAP_FLAGS flags;
            uint32_t heapOffset;
            uint32_t descriptorCount;
        };

        void Init(const Descriptor& descriptor);

        void Shutdown();

        DescriptorTable* Allocate(uint32_t count = 1);

        void DeAllocate(DescriptorTable* table, bool isPoolShutdown);

        bool RecycleObject(DescriptorTable* table);

        const Descriptor& GetDescriptor() const;

        D3D12_CPU_DESCRIPTOR_HANDLE GetD3D12CPUDescriptorTable(const DescriptorTable& table) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetD3D12GPUDescriptorTable(const DescriptorTable& table) const;

        uint32_t GetDescriptorHandleIncrementSize() const;
    
    private:
        using NodeIndex = int32_t;

        struct Node
        {
            bool IsValid() const
            {
                return size != 0;
            }

            NodeIndex nextFree = -1;
            size_t offset = 0;
            size_t size = 0;
        };

        struct FindNodeRequest
        {
            size_t size = 0;
        };

        struct FindNodeResponse
        {
            size_t remainingSize = 0;
            NodeIndex prevIndex = -1;
            NodeIndex nodeIndex = -1;
        };

        // Best fit algorithm to find a free node that fits the requested size
        void FindNode(const FindNodeRequest& request, FindNodeResponse& response);

        NodeIndex CreateNode();
        void ReleaseNode(NodeIndex index);

        NodeIndex InsertNode(NodeIndex prevIndex, size_t offset, size_t size);
        void RemoveNode(NodeIndex prevIndex, NodeIndex curIndex);

        void FreeInternal(const DescriptorTable& table);

        Descriptor m_descriptor;

        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart = {};
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart = {};
        uint32_t m_stride = 0;
        NodeIndex m_headIndex;
        eastl::unordered_map<size_t, DescriptorTable> m_tablePool;
        eastl::vector<NodeIndex> m_freeList;
        eastl::vector<Node> m_nodes;
    };
}