#include "BufferDescriptor.h"

namespace Spark::RHI
{
    BufferDescriptor::BufferDescriptor(BufferBindFlags bindFlags, size_t byteCount)
        : m_byteCount(byteCount)
        , m_bindFlags(bindFlags)
    {
    }
}
