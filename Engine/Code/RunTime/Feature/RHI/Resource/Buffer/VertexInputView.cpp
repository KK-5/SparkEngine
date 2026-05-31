/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "VertexInputView.h"

#include <EASTLEX/hash.h>
#include <Log/ILogSystem.h>

#include "Buffer.h"

namespace Spark::RHI
{
    VertexInputView::VertexInputView(
        const Buffer& buffer,
        uint32_t byteOffset,
        uint32_t byteCount,
        uint32_t byteStride)
        : m_buffer{&buffer}
        , m_byteOffset{byteOffset}
        , m_byteCount{byteCount}
        , m_byteStride{byteStride}
    {
        size_t seed = 0;
        eastl::hash_combine(seed, m_buffer);
        eastl::hash_combine(seed, m_byteOffset);
        eastl::hash_combine(seed, m_byteCount);
        eastl::hash_combine(seed, m_byteStride);
        m_hash = seed;
    }

    size_t VertexInputView::GetHash() const
    {
        return m_hash;
    }

    const Buffer* VertexInputView::GetBuffer() const
    {
        return m_buffer;
    }

    uint32_t VertexInputView::GetByteOffset() const
    {
        return m_byteOffset;
    }

    uint32_t VertexInputView::GetByteCount() const
    {
        return m_byteCount;
    }

    uint32_t VertexInputView::GetByteStride() const
    {
        return m_byteStride;
    }

    bool VertexInputView::operator==(const VertexInputView& other) const
    {
        return (m_hash == other.m_hash) &&
            (m_buffer == other.m_buffer) &&
            (m_byteOffset == other.m_byteOffset) &&
            (m_byteCount == other.m_byteCount) &&
            (m_byteStride == other.m_byteStride);
    }

    bool ValidateVertexInputViews(const RHI::InputStreamLayout& inputStreamLayout, eastl::span<const VertexInputView> vertexInputViews)
    {
        bool ok = true;

        if (Validation::isEnabled)
        {
            if (!inputStreamLayout.IsFinalized())
            {
                LOG_ERROR("[InputStreamLayout] InputStreamLayout is not finalized.");
                ok = false;
            }

            if (inputStreamLayout.GetStreamBuffers().size() != vertexInputViews.size())
            {
                LOG_ERROR("[InputStreamLayout] InputStreamLayout references {} stream buffers but {} VertexInputViews were provided.",
                    inputStreamLayout.GetStreamBuffers().size(), vertexInputViews.size());
                ok = false;
            }

            for (int i = 0; i < inputStreamLayout.GetStreamBuffers().size() && i < vertexInputViews.size(); ++i)
            {
                auto bufferDescriptors = inputStreamLayout.GetStreamBuffers();
                auto& bufferDescriptor = bufferDescriptors[i];
                auto& bufferView = vertexInputViews[i];

                // It can be valid to have a null buffer if this stream is not actually used by the shader, which can be the case for streams marked optional.
                if (bufferView.GetBuffer() == nullptr)
                {
                    continue;
                }

                if (bufferDescriptor.m_byteStride != bufferView.GetByteStride())
                {
                    LOG_ERROR("[InputStreamLayout] InputStreamLayout's buffer[{}] has stride={} but VertexInputView[{}] has stride={}.",
                        i, bufferDescriptor.m_byteStride, i, bufferView.GetByteStride());
                    ok = false;
                }
            }
        }

        return ok;
    }
}
