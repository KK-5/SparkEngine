#include "Buffer.h"

namespace Spark::RHI
{
    void Buffer::SetDescriptor(const BufferDescriptor& descriptor)
    {
        m_descriptor = descriptor;
    }

    const BufferDescriptor& Buffer::GetDescriptor() const
    {
        return m_descriptor;
    }

    ResourceState Buffer::GetResourceState() const
    {
        return m_resourceState;
    }

    void Buffer::SetResourceState(ResourceState state)
    {
        m_resourceState = state;
    }
}