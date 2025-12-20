#include "BufferViewDescriptor.h"

#include <Math/Interval.h>

namespace Spark::RHI
{
    BufferViewDescriptor CreateStructured(
        uint32_t elementOffset,
        uint32_t elementCount,
        uint32_t elementSize)
    {
        BufferViewDescriptor descriptor;
        descriptor.m_elementOffset = elementOffset;
        descriptor.m_elementCount = elementCount;
        descriptor.m_elementSize = elementSize;
        descriptor.m_elementFormat = Format::Unknown;
        return descriptor;
    }

    BufferViewDescriptor CreateRaw(
        uint32_t byteOffset,
        uint32_t byteCount)
    {
        BufferViewDescriptor descriptor;
        descriptor.m_elementOffset = byteOffset >> 2;
        descriptor.m_elementCount = byteCount >> 2;
        descriptor.m_elementSize = 4;
        descriptor.m_elementFormat = Format::R32_UINT;
        return descriptor;
    }

    BufferViewDescriptor BufferViewDescriptor::CreateTyped(
        uint32_t elementOffset,
        uint32_t elementCount,
        Format elementFormat)
    {
        BufferViewDescriptor descriptor;
        descriptor.m_elementOffset = elementOffset;
        descriptor.m_elementCount = elementCount;
        descriptor.m_elementSize = GetFormatSize(elementFormat);
        descriptor.m_elementFormat = elementFormat;
        return descriptor;
    }

    bool BufferViewDescriptor::operator==(const BufferViewDescriptor& other) const
    {
        return
            m_elementOffset == other.m_elementOffset &&
            m_elementCount == other.m_elementCount &&
            m_elementSize == other.m_elementSize &&
            m_elementFormat == other.m_elementFormat &&
            m_overrideBindFlags == other.m_overrideBindFlags;
    }

    bool BufferViewDescriptor::OverlapsSubResource(const BufferViewDescriptor& other) const
    {
        uint32_t begin = m_elementOffset * m_elementSize;
        uint32_t end = begin + m_elementCount * m_elementSize - 1;
        uint32_t otherBegin = other.m_elementOffset * other.m_elementSize;
        uint32_t otherEnd = otherBegin + other.m_elementCount * other.m_elementSize - 1;
        return Interval(begin, end).Overlaps(Interval(otherBegin, otherEnd));
    }
    
}