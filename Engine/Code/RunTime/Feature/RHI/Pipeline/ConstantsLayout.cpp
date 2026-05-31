/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ConstantsLayout.h"

#include <Log/ILogSystem.h>

namespace Spark::RHI
{
    ConstantsLayout::~ConstantsLayout()
    {
        Clear();
    }

    void ConstantsLayout::AddShaderInput(const ShaderInputConstantDescriptor& descriptor)
    {
        m_inputs.push_back(descriptor);
    }

    void ConstantsLayout::Clear()
    {
        m_inputs.clear();
        m_nameMap.clear();
        m_sizeInBytes = 0;
        m_hash = 0;
    }

    bool ConstantsLayout::IsFinalized() const
    {
        return m_hash != 0;
    }

    size_t ConstantsLayout::GetHash() const
    {
        return m_hash;
    }

    ShaderInputIndex ConstantsLayout::FindShaderInputIndex(const ShaderInputName& name) const
    {
        auto it = m_nameMap.find(name);
        if (it != m_nameMap.end())
        {
            return it->second;
        }
        return InvalidShaderInputIndex;
    }

    Interval ConstantsLayout::GetInterval(ShaderInputIndex inputIndex) const
    {
        const ShaderInputConstantDescriptor& desc = GetShaderInput(inputIndex);
        uint32_t start = desc.m_constantByteOffset;
        uint32_t end = start + desc.m_constantByteCount;
        return Interval(start, end);
    }

    const ShaderInputConstantDescriptor& ConstantsLayout::GetShaderInput(ShaderInputIndex inputIndex) const
    {
        const auto index = inputIndex;
        static const ShaderInputConstantDescriptor invalidDescriptor;
        return index < m_inputs.size() ? m_inputs[index] : invalidDescriptor;
    }

    eastl::span<const ShaderInputConstantDescriptor> ConstantsLayout::GetShaderInputList() const
    {
        return m_inputs;
    }

    uint32_t ConstantsLayout::GetDataSize() const
    {
        return m_sizeInBytes;
    }

    bool ConstantsLayout::ValidateAccess(ShaderInputIndex inputIndex) const
    {
        if (Validation::isEnabled)
        {
            uint32_t count = static_cast<uint32_t>(m_inputs.size());
            if (inputIndex >= count)
            {
                ASSERT(false, "Inline Constant Input index '{}' out of range [0,{}).", inputIndex, count);
                return false;
            }
        }

        return true;
    }

    bool ConstantsLayout::ValidateConstantInputs() const
    {
        if (Validation::isEnabled)
        {
            if (!m_sizeInBytes)
            {
                ASSERT(m_nameMap.empty(), "Constants size is not valid.");
                return m_nameMap.empty();
            }
        }

        return true;
    }

    bool ConstantsLayout::Finalize()
    {
        m_hash = 0;
        uint32_t constantInputIndex = 0;
        uint32_t constantDataSize = 0;
        for (const auto& constantDescriptor : m_inputs)
        {
            const ShaderInputIndex inputIndex(constantInputIndex);
            if (m_nameMap.find(constantDescriptor.m_name) != m_nameMap.end())
            {
                Clear();
                return false;
            }

            m_nameMap.emplace(constantDescriptor.m_name, inputIndex);

            uint32_t end = constantDescriptor.m_constantByteOffset + constantDescriptor.m_constantByteCount;
            constantDataSize = eastl::max(constantDataSize, end);

            ++constantInputIndex;
            eastl::hash_combine_raw(m_hash, constantDescriptor.GetHash());
        }

        m_sizeInBytes = constantDataSize;

        if (!ValidateConstantInputs())
        {
            Clear();
            return false;
        }

        return true;
    }
}