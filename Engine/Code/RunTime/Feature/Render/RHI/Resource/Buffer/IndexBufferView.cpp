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
        m_hash = eastl::hash<const IndexBufferView*>()(this);
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
}