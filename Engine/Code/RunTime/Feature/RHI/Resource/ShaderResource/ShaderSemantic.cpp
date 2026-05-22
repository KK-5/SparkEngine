/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ShaderSemantic.h"

#include <EASTL/functional.h>

namespace Spark::RHI
{
    ShaderSemantic::ShaderSemantic(eastl::string_view name, size_t index)
        : m_name(name.data(), name.size())
        , m_index(static_cast<uint32_t>(index))
    {}

    bool ShaderSemantic::operator==(const ShaderSemantic& rhs) const
    {
        return this->m_index == rhs.m_index && this->m_name == rhs.m_name;
    }

    size_t ShaderSemantic::GetHash() const
    {
        size_t h1 = eastl::hash<eastl::string>()(m_name);
        size_t h2 = eastl::hash<uint32_t>()(m_index);

        size_t combinedHash = h1;
        combinedHash ^= h2 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
        return combinedHash;
    }

    eastl::string ShaderSemantic::ToString() const
    {
        return m_name + eastl::to_string(m_index);
    }
}