#pragma once

#include "BufferViewDescriptor.h"
#include <Resource/ResourceView.h>

namespace Spark::RHI
{
    class Buffer;

    class BufferView : public ResourceView
    {
    public:
        virtual ~BufferView() = default;

        static constexpr uint32_t InvalidBindlessIndex = static_cast<uint32_t>(-1);
        static constexpr uint64_t InvalidDeviceAddress = static_cast<uint64_t>(-1);

        ResultCode Init(const Buffer& buffer, const BufferViewDescriptor& viewDescriptor);

        const BufferViewDescriptor& GetDescriptor() const;

        const Buffer& GetBuffer() const;

        bool IsFullView() const override final;

        virtual uint32_t GetBindlessReadIndex() const
        {
            return InvalidBindlessIndex;
        }

        virtual uint32_t GetBindlessReadWriteIndex() const
        {
            return InvalidBindlessIndex;
        }

        virtual uint64_t GetDeviceAddress() const
        {
            return InvalidDeviceAddress;
        }
    
    private:
        void Shutdown() override final;

        bool ValidateForInit(const Buffer& buffer, const BufferViewDescriptor& viewDescriptor) const;

        BufferViewDescriptor m_descriptor;
    };
}