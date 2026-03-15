/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

 /*
 * Modified by SparkEngine in 2025
 *  -- Rename BufferMemoryType::SubAllocated to BufferMemoryType::Shared
 */
#pragma once

#include <EASTL/internal/move_help.h>
#include <MemoryView.h>

namespace Spark::RHI::DX12
{
    enum class BufferMemoryType : uint32_t
    {
        /// This buffer owns its own memory resource. Attachments on the frame scheduler require
        /// this type.
        Unique,

        /// This buffer shares a memory resource with other buffers.
        Shared
    };

    class BufferMemoryView : public MemoryView
    {
    public:
        BufferMemoryView() = default;
        BufferMemoryView(MemoryView&& memoryView, BufferMemoryType memoryType);

        /// Supports only move construction / assignment because of constraints in the base class.
        BufferMemoryView(const BufferMemoryView& rhs) = delete;
        BufferMemoryView(BufferMemoryView&& rhs) = default;
        BufferMemoryView& operator=(const BufferMemoryView& rhs) = delete;
        BufferMemoryView& operator=(BufferMemoryView&& rhs) = default;

        BufferMemoryType GetType() const;

    private:
        BufferMemoryType m_type = BufferMemoryType::Unique;
    };


}