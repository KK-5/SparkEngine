/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include "PipelineLayoutDescriptor.h"

#include <EASTLEX/hash.h>

namespace Spark::RHI::DX12
{
    RootConstantBinding::RootConstantBinding(
        uint32_t constantCount,
        uint32_t constantRegister,
        uint32_t constantRegisterSpace)
        : m_constantCount(constantCount)
        , m_constantRegister(constantRegister)
        , m_constantRegisterSpace(constantRegisterSpace)
    {
    }

    size_t RootConstantBinding::GetHash(size_t seed) const
    {
        eastl::hash_combine(seed, m_constantCount, m_constantRegister, m_constantRegisterSpace);
        return seed;
    }

    size_t ShaderResourceGroupVisibility::GetHash(size_t seed) const
    {
        eastl::hash_combine(seed, m_descriptorTableShaderStageMask);
        return seed;
    }

    void PipelineLayoutDescriptor::SetRootConstantBinding(const RootConstantBinding& rootConstantBinding)
    {
        m_rootConstantBinding = rootConstantBinding;
    }

    const RootConstantBinding& PipelineLayoutDescriptor::GetRootConstantBinding() const
    {
        return m_rootConstantBinding;
    }

    void PipelineLayoutDescriptor::AddShaderResourceGroupVisibility(const ShaderResourceGroupVisibility& shaderResourceGroupVisibility)
    {
        m_shaderResourceGroupVisibilities.push_back(shaderResourceGroupVisibility);
    }

    const ShaderResourceGroupVisibility& PipelineLayoutDescriptor::GetShaderResourceGroupVisibility(uint32_t index) const
    {
        return m_shaderResourceGroupVisibilities[index];
    }

    size_t PipelineLayoutDescriptor::GetHashInternal(size_t seed) const
    {
        eastl::hash_combine_raw(seed, m_rootConstantBinding.GetHash());
        for (const auto& visibility : m_shaderResourceGroupVisibilities)
        {
            eastl::hash_combine_raw(seed, visibility.GetHash());
        }
        return seed;
    }
}