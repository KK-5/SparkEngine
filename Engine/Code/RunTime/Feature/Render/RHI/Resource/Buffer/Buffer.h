#pragma once

#include <EASTL/atomic.h>

#include <Resource/Resource.h>
#include <Resource/ResourceViewCache.h>
#include "BufferDescriptor.h"
#include "BufferView.h"
#include "BufferViewDescriptor.h"

namespace Spark::RHI
{
    class BufferFrameAttachment;
    class BufferView;

    class Buffer : public Resource
    {
        friend class BufferPool;  // for SetDescriptor, m_mapRefCount
    public:
        virtual ~Buffer() = default;

        const BufferDescriptor& GetDescriptor() const;

        const BufferFrameAttachment* GetFrameAttachment() const;

        Ptr<BufferView> GetBufferView(const BufferViewDescriptor& bufferViewDescriptor) const;

        void EraseBufferView(BufferView* bufferView) const;

        bool IsInBufferCache(const BufferViewDescriptor& bufferViewDescriptor);

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
        mutable ResourceViewCache<BufferViewDescriptor, BufferViewDescriptoHasher> m_bufferViewCache;
        eastl::atomic<uint32_t> m_mapRefCount {0};
    };
    
}