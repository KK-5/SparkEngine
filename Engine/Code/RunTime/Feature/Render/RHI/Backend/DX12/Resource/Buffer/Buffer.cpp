/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "Buffer.h"

namespace Spark::RHI::DX12
{
    const BufferMemoryView& Buffer::GetMemoryView() const
    {
        return m_memoryView;
    }

    BufferMemoryView& Buffer::GetMemoryView()
    {
        return m_memoryView;
    }

    uint64_t Buffer::GetDeviceAddress() const
    {
        return m_memoryView.GetGpuAddress();
    }
}