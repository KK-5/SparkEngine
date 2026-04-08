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
    ShaderInputBufferDescriptor::ShaderInputBufferDescriptor(
        const InputName& name,
        ShaderInputBufferAccess access,
        ShaderInputBufferType type,
        uint32_t bufferCount,
        uint32_t strideSize,
        uint32_t registerId,
        uint32_t spaceId)
        : m_name{ name }
        , m_access{ access }
        , m_type{ type }
        , m_count{ bufferCount }
        , m_strideSize{ strideSize }
        , m_registerId{ registerId }
        , m_spaceId{ spaceId }
    {}

    size_t ShaderInputBufferDescriptor::GetHash(size_t seed) const
    {
        size_t nameHash = m_name.GetHash();
        eastl::hash_combine_raw(seed, nameHash);
        eastl::hash_combine(seed, m_access, m_type, m_count, m_strideSize, m_registerId);
        return seed;
    }

    ShaderInputImageDescriptor::ShaderInputImageDescriptor(
        const InputName& name,
        ShaderInputImageAccess access,
        ShaderInputImageType type,
        uint32_t imageCount,
        uint32_t registerId,
        uint32_t spaceId)
        : m_name{ name }
        , m_access{ access }
        , m_type{ type }
        , m_count{ imageCount }
        , m_registerId{ registerId }
        , m_spaceId{ spaceId }
    {}

    size_t ShaderInputImageDescriptor::GetHash(size_t seed) const
    {
        size_t nameHash = m_name.GetHash();
        eastl::hash_combine_raw(seed, nameHash);
        eastl::hash_combine(seed, m_access, m_type, m_count, m_registerId);
        return seed;
    }

    ShaderInputBufferUnboundedArrayDescriptor::ShaderInputBufferUnboundedArrayDescriptor(
        const InputName& name,
        ShaderInputBufferAccess access,
        ShaderInputBufferType type,
        uint32_t strideSize,
        uint32_t registerId,
        uint32_t spaceId)
        : m_name{ name }
        , m_access{ access }
        , m_type{ type }
        , m_strideSize { strideSize }
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
        , m_access{ access }
        , m_type { type }
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

    ShaderInputSamplerDescriptor::ShaderInputSamplerDescriptor(
        const InputName& name,
        uint32_t samplerCount,
        uint32_t registerId,
        uint32_t spaceId)
        : m_name{ name }
        , m_count{ samplerCount }
        , m_registerId{ registerId }
        , m_spaceId{ spaceId }
    {}

    size_t ShaderInputSamplerDescriptor::GetHash(size_t seed) const
    {
        size_t nameHash = m_name.GetHash();
        eastl::hash_combine_raw(seed, nameHash);
        eastl::hash_combine(seed, m_count, m_registerId);
        return seed;
    }

    ShaderInputConstantDescriptor::ShaderInputConstantDescriptor(
        const InputName& name,
        uint32_t constantByteOffset,
        uint32_t constantByteCount,
        uint32_t registerId,
        uint32_t spaceId)
        : m_name{ name }
        , m_constantByteOffset{ constantByteOffset }
        , m_constantByteCount{ constantByteCount }
        , m_registerId{ registerId }
        , m_spaceId{ spaceId }
    {}

    size_t ShaderInputConstantDescriptor::GetHash(size_t seed) const
    {
        size_t nameHash = m_name.GetHash();
        eastl::hash_combine_raw(seed, nameHash);
        eastl::hash_combine(seed, m_constantByteOffset, m_constantByteCount, m_registerId);
        return seed;
    }

    ShaderInputStaticSamplerDescriptor::ShaderInputStaticSamplerDescriptor(
        const InputName& name, const SamplerState& samplerState, uint32_t registerId, uint32_t spaceId)
        : m_name{name}
        , m_samplerState{ samplerState }
        , m_registerId{ registerId }
        , m_spaceId{ spaceId }
    {}

    size_t ShaderInputStaticSamplerDescriptor::GetHash(size_t seed) const
    {
        size_t nameHash = m_name.GetHash();
        eastl::hash_combine_raw(seed, nameHash);
        SamplerStateHasher hasher;
        size_t samplerStateHash = hasher(m_samplerState);
        eastl::hash_combine_raw(seed, nameHash);
        eastl::hash_combine(seed, m_registerId);
        return seed;
    }
}