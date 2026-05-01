#pragma once

#include <EASTL/atomic.h>

#include <RHI/Resource/Resource.h>
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
    public:
        virtual ~Buffer() = default;

        const BufferDescriptor& GetDescriptor() const;

        static constexpr uint64_t InvalidDeviceAddress = static_cast<uint64_t>(-1);
        virtual uint64_t GetDeviceAddress() const
        {
            return InvalidDeviceAddress;
        }

    protected:
        Buffer() = default;

        void SetDescriptor(const BufferDescriptor& descriptor);
    
    private:
        BufferDescriptor m_descriptor;
        eastl::atomic<uint32_t> m_mapRefCount {0};
    };
    
}