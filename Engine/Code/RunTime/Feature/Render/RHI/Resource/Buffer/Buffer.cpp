#include "Buffer.h"

namespace Spark::Render::RHI
{
    void Buffer::SetDescriptor(const BufferDescriptor& descriptor)
    {
        m_descriptor = descriptor;
    }

    const BufferDescriptor& Buffer::GetDescriptor() const
    {
        return m_descriptor;
    }

    const BufferFrameAttachment* Buffer::GetFrameAttachment() const
    {
        //return static_cast<const BufferFrameAttachment*>(GetResourceView()->GetFrameAttachment());
        return nullptr;
    }

    Ptr<BufferView> Buffer::GetBufferView(const BufferViewDescriptor& bufferViewDescriptor) const
    {
        ResourceView* view = m_bufferViewCache.GetResourceView(bufferViewDescriptor);
        return static_cast<BufferView*>(view);
    }

    void Buffer::EraseBufferView(BufferView* bufferView) const
    {
        m_bufferViewCache.EraseResourceView(bufferView);
    }

    bool Buffer::IsInBufferCache(const BufferViewDescriptor& bufferViewDescriptor)
    {
        return m_bufferViewCache.IsInResourceCache(bufferViewDescriptor);
    }
}