/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <RHI/Base.h>

namespace Spark::RHI
{
    enum class IndexFormat : uint32_t
    {
        Unknown = 0,
        UINT16,
        UINT32,
    };

    inline uint32_t GetIndexFormatSize(IndexFormat format)
    {
        switch (format)
        {
        case IndexFormat::UINT16:
            return 2;
        case IndexFormat::UINT32:
            return 4;
        default:
            return 0;
        }
    }

    class Buffer;

    class alignas(8) IndexBufferView
    {
    public:
        IndexBufferView() = default;

        IndexBufferView(
            const Buffer& buffer,
            uint32_t byteOffset,
            uint32_t byteCount,
            IndexFormat format);

        //! Returns the hash of the view. This hash is precomputed at creation time.
        size_t GetHash() const;

        //! Returns the buffer associated with the data in the view.
        const Buffer* GetBuffer() const;

        //! Returns the byte offset into the buffer returned by GetBuffer
        uint32_t GetByteOffset() const;

        //! Returns the number of bytes in the view.
        uint32_t GetByteCount() const;

        //! Returns the format of each index in the view.
        IndexFormat GetIndexFormat() const;

    private:
        size_t m_hash { 0 };
        const Buffer* m_buffer = nullptr;
        uint32_t m_byteOffset = 0;
        uint32_t m_byteCount = 0;
        IndexFormat m_format = IndexFormat::UINT32;
    };
}