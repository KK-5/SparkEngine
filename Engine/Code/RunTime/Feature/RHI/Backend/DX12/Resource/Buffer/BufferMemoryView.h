/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

 /*
 * Modified by SparkEngine in 2025
 *  -- Remove BufferMemoryType: a buffer's memory is a resource of its own.
 */
#pragma once

#include <EASTL/internal/move_help.h>
#include <MemoryView.h>

namespace Spark::RHI::DX12
{
    class BufferMemoryView : public MemoryView
    {
    public:
        BufferMemoryView() = default;
        explicit BufferMemoryView(MemoryView&& memoryView);

        /// Supports only move construction / assignment because of constraints in the base class.
        BufferMemoryView(const BufferMemoryView& rhs) = delete;
        BufferMemoryView(BufferMemoryView&& rhs) = default;
        BufferMemoryView& operator=(const BufferMemoryView& rhs) = delete;
        BufferMemoryView& operator=(BufferMemoryView&& rhs) = default;
    };
}
