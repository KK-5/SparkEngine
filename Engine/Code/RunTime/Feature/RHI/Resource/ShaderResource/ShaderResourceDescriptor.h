/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2026
 *  -- ShaderInputXxxDescriptor + ShaderInputType / Access / Type enums are owned by
 *     <RHI/Resource/ShaderInput/ShaderInputDescriptor.h>. This header re-exposes them
 *     via include and only declares the legacy UnboundedArray descriptors still used
 *     by the old SRG path (ShaderResourceLayout, ShaderResource). When the old SRG
 *     path is removed, this header and the corresponding .cpp can be deleted entirely.
 */
#pragma once

#include <Math/Interval.h>
#include <Object/ObjectName.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/ShaderInput/ShaderInputDescriptor.h>

namespace Spark::RHI
{
    class ShaderInputBufferUnboundedArrayDescriptor final
    {
    public:
        ShaderInputBufferUnboundedArrayDescriptor() = default;
        ShaderInputBufferUnboundedArrayDescriptor(
            const InputName& name,
            ShaderInputBufferAccess access,
            ShaderInputBufferType type,
            uint32_t strideSize,
            uint32_t registerId,
            uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        InputName m_name;

        ShaderInputBufferType m_type = ShaderInputBufferType::Unknown;

        ShaderInputBufferAccess m_access = ShaderInputBufferAccess::Read;

        uint32_t m_strideSize = 0;

        uint32_t m_registerId = UndefinedRegisterSlot;

        uint32_t m_spaceId = UndefinedRegisterSlot;
    };

    class ShaderInputImageUnboundedArrayDescriptor final
    {
    public:
        ShaderInputImageUnboundedArrayDescriptor() = default;
        ShaderInputImageUnboundedArrayDescriptor(
            const InputName& name,
            ShaderInputImageAccess access,
            ShaderInputImageType type,
            uint32_t registerId,
            uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        InputName m_name;

        ShaderInputImageType m_type = ShaderInputImageType::Unknown;

        ShaderInputImageAccess m_access = ShaderInputImageAccess::Read;

        uint32_t m_registerId = UndefinedRegisterSlot;

        uint32_t m_spaceId = UndefinedRegisterSlot;
    };
}
