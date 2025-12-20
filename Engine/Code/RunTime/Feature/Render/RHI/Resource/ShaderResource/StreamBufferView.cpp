#include "StreamBufferView.h"

#include <EASTLEX/hash.h>
#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    StreamBufferView::StreamBufferView(
        const Buffer& buffer,
        uint32_t byteOffset,
        uint32_t byteCount,
        uint32_t byteStride)
        : m_buffer{&buffer}
        , m_byteOffset{byteOffset}
        , m_byteCount{byteCount}
        , m_byteStride{byteStride}
    {
        size_t hash = eastl::hash<const Buffer*>()(m_buffer);
        eastl::hash_combine(hash, m_byteOffset, m_byteCount, m_byteStride);
        m_hash = hash;
    }

    size_t StreamBufferView::GetHash() const
    {
        return m_hash;
    }

    const Buffer* StreamBufferView::GetBuffer() const
    {
        return m_buffer;
    }

    uint32_t StreamBufferView::GetByteOffset() const
    {
        return m_byteOffset;
    }

    uint32_t StreamBufferView::GetByteCount() const
    {
        return m_byteCount;
    }

    uint32_t StreamBufferView::GetByteStride() const
    {
        return m_byteStride;
    }

    bool StreamBufferView::operator==(const StreamBufferView& other) const
    {
        return (m_hash == other.m_hash) &&
            (m_buffer == other.m_buffer) &&
            (m_byteOffset == other.m_byteOffset) &&
            (m_byteCount == other.m_byteCount) &&
            (m_byteStride == other.m_byteStride);
    }

    bool ValidateStreamBufferViews(const RHI::InputStreamLayout& inputStreamLayout, eastl::span<const StreamBufferView> streamBufferViews)
    {
        bool ok = true;

        if (Validation::isEnabled)
        {
            if (!inputStreamLayout.IsFinalized())
            {
                LOG_ERROR("[InputStreamLayout] InputStreamLayout is not finalized.");
                ok = false;
            }

            if (inputStreamLayout.GetStreamBuffers().size() != streamBufferViews.size())
            {
                LOG_ERROR("[InputStreamLayout] InputStreamLayout references {} stream buffers but {} StreamBufferViews were provided.",
                    inputStreamLayout.GetStreamBuffers().size(), streamBufferViews.size());
                ok = false;
            }

            for (int i = 0; i < inputStreamLayout.GetStreamBuffers().size() && i < streamBufferViews.size(); ++i)
            {
                auto bufferDescriptors = inputStreamLayout.GetStreamBuffers();
                auto& bufferDescriptor = bufferDescriptors[i];
                auto& bufferView = streamBufferViews[i];

                // It can be valid to have a null buffer if this stream is not actually used by the shader, which can be the case for streams marked optional.
                if (bufferView.GetBuffer() == nullptr)
                {
                    continue;
                }

                if (bufferDescriptor.m_byteStride != bufferView.GetByteStride())
                {
                    LOG_ERROR("[InputStreamLayout] InputStreamLayout's buffer[{}] has stride={} but DeviceStreamBufferView[{}] has stride={}.",
                        i, bufferDescriptor.m_byteStride, i, bufferView.GetByteStride());
                    ok = false;
                }
            }
        }

        return ok;
    }
}