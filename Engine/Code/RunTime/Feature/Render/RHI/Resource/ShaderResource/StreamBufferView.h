#pragma once

#include <Resource/Buffer/Buffer.h>

#include "InputStreamLayout.h"

namespace Spark::Render::RHI
{
    class alignas(8) StreamBufferView
    {
    public:
        StreamBufferView() = default;

        StreamBufferView(
            const Buffer& buffer,
            uint32_t byteOffset,
            uint32_t byteCount,
            uint32_t byteStride);

        size_t GetHash() const;

        const Buffer* GetBuffer() const;

        uint32_t GetByteOffset() const;

        uint32_t GetByteCount() const;

        //! Returns the distance in bytes between consecutive vertex entries in the buffer.
        //! This must match the stride value in StreamBufferDescriptor.
        uint32_t GetByteStride() const;

        bool operator==(const StreamBufferView& other) const;

    private:
        size_t m_hash = 0;
        const Buffer* m_buffer = nullptr;
        uint32_t m_byteOffset = 0;
        uint32_t m_byteCount = 0;
        uint32_t m_byteStride = 0;
    };

    bool ValidateStreamBufferViews(const InputStreamLayout& inputStreamLayout, eastl::span<const StreamBufferView> streamBufferViews);
}