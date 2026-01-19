#pragma once

#include <RHI/Device/DeviceObject.h>
#include <3rdParty/D3D12MA/D3D12MemAlloc.h>
#include <ReleaseQueue.h>
#include <MemoryView.h>

namespace Spark::RHI::DX12
{
    class Device;
    class Buffer;

    // StagingMemoryContext 保证线程安全
    class StagingMemoryContext final : public RHI::DeviceObject
    {
    public:
        StagingMemoryContext() = default;
        ~StagingMemoryContext() noexcept = default;

        RHI::ResultCode Init(Device& device);

        MemoryView AcquireStagingMemory(size_t size, size_t alignment);
    
    private:
        Ptr<D3D12MA::Allocator> m_allocator;
        D3D12MAReleaseQueue m_releaseQueue;
    };

}

