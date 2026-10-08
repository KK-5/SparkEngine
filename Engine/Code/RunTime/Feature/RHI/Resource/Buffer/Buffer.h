#pragma once

#include <EASTL/atomic.h>

#include <RHI/Resource/Resource.h>
#include <RHI/Resource/ResourceState.h>
#include "BufferDescriptor.h"
#include "BufferView.h"
#include "BufferViewDescriptor.h"

namespace Spark::RHI
{
    class BufferView;

    class Buffer : public Resource
    {
        friend class BufferPool;  // for SetDescriptor, m_mapRefCount
        friend class TransientResourcePool;
        friend class ResourcePool;  // for SetResourceState
        friend class CommandList;   // for SetResourceState (barrier updates)
    public:
        virtual ~Buffer() = default;

        const BufferDescriptor& GetDescriptor() const;

        ResourceState GetResourceState() const;

        static constexpr uint64_t InvalidDeviceAddress = static_cast<uint64_t>(-1);
        virtual uint64_t GetDeviceAddress() const
        {
            return InvalidDeviceAddress;
        }

    protected:
        Buffer() = default;

        void SetDescriptor(const BufferDescriptor& descriptor);
    
    private:
        void SetResourceState(ResourceState state);

        BufferDescriptor m_descriptor;
        ResourceState m_resourceState;
        eastl::atomic<uint32_t> m_mapRefCount {0};
    };
    
}