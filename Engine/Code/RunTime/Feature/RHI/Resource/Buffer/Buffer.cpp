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
}