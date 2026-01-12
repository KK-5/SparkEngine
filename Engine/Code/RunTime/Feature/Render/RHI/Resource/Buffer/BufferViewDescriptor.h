#pragma once

#include <EASTL/functional.h>

#include <RHI/Format.h>
#include "BufferBindFlags.h"

namespace Spark::RHI
{
    struct BufferViewDescriptor
    {
        BufferViewDescriptor() = default;

        bool operator==(const BufferViewDescriptor& other) const;

        static BufferViewDescriptor CreateStructured(
            uint32_t elementOffset,
            uint32_t elementCount,
            uint32_t elementSize);

        static BufferViewDescriptor CreateRaw(
            uint32_t byteOffset,
            uint32_t byteCount);

        static BufferViewDescriptor CreateTyped(
            uint32_t elementOffset,
            uint32_t elementCount,
            Format elementFormat);

        /*
        static BufferViewDescriptor CreateRayTracingTLAS(
            uint32_t totalByteCount);

        HashValue64 GetHash(HashValue64 seed = HashValue64{ 0 }) const;
        */

        bool OverlapsSubResource(const BufferViewDescriptor& other) const;

        uint32_t m_elementOffset = 0;
        uint32_t m_elementCount = 0;
        uint32_t m_elementSize = 0;
        Format m_elementFormat = Format::Unknown;
        BufferBindFlags m_overrideBindFlags = BufferBindFlags::None;
    };

    struct BufferViewDescriptoHasher {
        size_t operator()(const BufferViewDescriptor& descriptor) const {
            size_t h1 = eastl::hash<uint32_t>()(descriptor.m_elementOffset);
            size_t h2 = eastl::hash<uint32_t>()(descriptor.m_elementCount);
            size_t h3 = eastl::hash<uint32_t>()(descriptor.m_elementSize);
            size_t h4 = eastl::hash<uint32_t>()(static_cast<uint32_t>(descriptor.m_elementFormat));
            size_t h5 = eastl::hash<uint32_t>()(static_cast<uint32_t>(descriptor.m_overrideBindFlags));

            // Combine the hash values
            size_t combinedHash = h1;
            combinedHash ^= h2 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
            combinedHash ^= h3 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
            combinedHash ^= h4 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
            combinedHash ^= h5 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);

            return combinedHash;
        }
    };
}