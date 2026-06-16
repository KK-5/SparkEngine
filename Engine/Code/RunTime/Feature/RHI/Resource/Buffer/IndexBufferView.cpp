/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "IndexBufferView.h"

#include <EASTLEX/hash.h>

#include "Buffer.h"

namespace Spark::RHI
{
    IndexBufferView::IndexBufferView(
        const Buffer& buffer,
        uint32_t byteOffset,
        uint32_t byteCount,
        IndexFormat format)
        : m_buffer{&buffer}
        , m_byteOffset{byteOffset}
        , m_byteCount{byteCount}
        , m_format{format}
    {
        size_t seed = 0;
        eastl::hash_combine(seed, m_buffer);
        eastl::hash_combine(seed, m_byteOffset);
        eastl::hash_combine(seed, m_byteCount);
        eastl::hash_combine(seed, static_cast<uint32_t>(m_format));
        m_hash = seed;
    }

    size_t IndexBufferView::GetHash() const
    {
        return m_hash;
    }

    const Buffer* IndexBufferView::GetBuffer() const
    {
        return m_buffer;
    }

    uint32_t IndexBufferView::GetByteOffset() const
    {
        return m_byteOffset;
    }

    uint32_t IndexBufferView::GetByteCount() const
    {
        return m_byteCount;
    }

    IndexFormat IndexBufferView::GetIndexFormat() const
    {
        return m_format;
    }

    bool IndexBufferView::operator==(const IndexBufferView& other) const
    {
        return (m_hash == other.m_hash) &&
            (m_buffer == other.m_buffer) &&
            (m_byteOffset == other.m_byteOffset) &&
            (m_byteCount == other.m_byteCount) &&
            (m_format == other.m_format);
    }
}