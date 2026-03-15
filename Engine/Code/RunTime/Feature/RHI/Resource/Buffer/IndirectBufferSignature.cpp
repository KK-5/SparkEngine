/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "IndirectBufferSignature.h"

#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    ResultCode IndirectBufferSignature::Init(Device& device, const IndirectBufferSignatureDescriptor& descriptor)
    {
        ResultCode result = InitInternal(device, descriptor);
        if (result == ResultCode::Success)
        {
            DeviceObject::Init(device);
            m_descriptor = descriptor;
        }

        return result;
    }

    uint32_t IndirectBufferSignature::GetByteStride() const
    {
        ASSERT(IsInitialized(), "Signature is not initialized");
        return GetByteStrideInternal();
    }

    uint32_t IndirectBufferSignature::GetOffset(IndirectCommandIndex index) const
    {
        ASSERT(IsInitialized(), "Signature is not initialized");
        if (Validation::isEnabled)
        {
            if (index == InvalidIndirectCommandIndex)
            {
                ASSERT(false, "Invalid index");
                return 0;
            }

            if (index >= m_descriptor.m_layout.GetCommands().size())
            {
                ASSERT(false, "Index {} is greater than the number of commands on the layout", index);
                return 0;
            }
        }

        return GetOffsetInternal(index);
    }

    const IndirectBufferSignatureDescriptor& IndirectBufferSignature::GetDescriptor() const
    {
        return m_descriptor;
    }

    const IndirectBufferLayout& IndirectBufferSignature::GetLayout() const
    {
        return m_descriptor.m_layout;
    }

    void IndirectBufferSignature::Shutdown()
    {
        ShutdownInternal();
        DeviceObject::Shutdown();
    }
}