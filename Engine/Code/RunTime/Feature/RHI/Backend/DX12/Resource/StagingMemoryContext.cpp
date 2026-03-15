#include "StagingMemoryContext.h"

#include <Device/Device.h>

namespace Spark::RHI::DX12
{
    RHI::ResultCode StagingMemoryContext::Init(Device& device)
    {
        DeviceObject::Init(device);

        D3D12MA::ALLOCATOR_DESC desc = {};
        desc.pDevice = device.GetDevice();
        desc.pAdapter = device.GetPhysicalDevice().GetAdapter();

        D3D12MA::Allocator* pAllocator;
        if (FAILED(D3D12MA::CreateAllocator(&desc, &pAllocator)))
        {
            LOG_ERROR("[StagingMemoryContext] Failed to initialize the D3D12MemoryAllocator.");
            return RHI::ResultCode::Fail;
        }
        m_allocator = pAllocator;

        D3D12MAReleaseQueue::Descriptor releaseQueueDescriptor;
        releaseQueueDescriptor.m_collectLatency = device.GetDescriptor().m_frameCountMax;
        m_releaseQueue.Init(releaseQueueDescriptor);

        return RHI::ResultCode::Success;
    }

    MemoryView StagingMemoryContext::AcquireStagingMemory(size_t size, size_t alignment)
    {
        CD3DX12_RESOURCE_DESC desc =  CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_NONE, alignment);

        D3D12MA::ALLOCATION_DESC allocDesc = {};
        allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

        D3D12MA::Allocation* allocation = nullptr;
        HRESULT result = m_allocator->CreateResource(
            &allocDesc,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            &allocation,
            IID_NULL,
            NULL
        );
        ASSERT(result == S_OK, "[StagingMemoryContext] D3D12MA Create buffer resource failed!");

        // 默认MemoryView使用了Memory(ID3DResource)全部资源
        MemoryView memoryView(allocation, MemoryViewType::Buffer, 0, allocation->GetSize(), allocation->GetAlignment());
        // Queue the memory or deferred release immediately.
        m_releaseQueue.QueueForCollect(allocation);
        return memoryView;
    }
}