/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ShaderResourceDescriptor.h"

#include <EASTLEX/hash.h>

namespace Spark::RHI
{
    ShaderInputBufferUnboundedArrayDescriptor::ShaderInputBufferUnboundedArrayDescriptor(
        const InputName& name,
        ShaderInputBufferAccess access,
        ShaderInputBufferType type,
        uint32_t strideSize,
        uint32_t registerId,
        uint32_t spaceId)
        : m_name{ name }
        , m_type{ type }
        , m_access{ access }
        , m_strideSize{ strideSize }
        , m_registerId{ registerId }
        , m_spaceId{ spaceId }
    {}

    size_t ShaderInputBufferUnboundedArrayDescriptor::GetHash(size_t seed) const
    {
        size_t nameHash = m_name.GetHash();
        eastl::hash_combine_raw(seed, nameHash);
        eastl::hash_combine(seed, m_access, m_type, m_strideSize, m_registerId);
        return seed;
    }

    ShaderInputImageUnboundedArrayDescriptor::ShaderInputImageUnboundedArrayDescriptor(
        const InputName& name,
        ShaderInputImageAccess access,
        ShaderInputImageType type,
        uint32_t registerId,
        uint32_t spaceId)
        : m_name{ name }
        , m_type{ type }
        , m_access{ access }
        , m_registerId{ registerId }
        , m_spaceId{ spaceId }
    {}

    size_t ShaderInputImageUnboundedArrayDescriptor::GetHash(size_t seed) const
    {
        size_t nameHash = m_name.GetHash();
        eastl::hash_combine_raw(seed, nameHash);
        eastl::hash_combine(seed, m_access, m_type, m_registerId);
        return seed;
    }
}
