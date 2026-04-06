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

    size_t ShaderResourceVisibility::GetHash(size_t seed) const
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

    void PipelineLayoutDescriptor::AddShaderResourceVisibility(const ShaderResourceVisibility& shaderResourceVisibility)
    {
        m_shaderResourceVisibilities.push_back(shaderResourceVisibility);
    }

    const ShaderResourceVisibility& PipelineLayoutDescriptor::GetShaderResourceVisibility(uint32_t index) const
    {
        return m_shaderResourceVisibilities[index];
    }

    ResultCode PipelineLayoutDescriptor::FinalizeInternal()
    {
        auto shaderResourceLayoutInfo = GetShaderResourceLayoutInfo();
        for (auto layoutInfo: shaderResourceLayoutInfo)
        {
            RHI::ShaderResourceBindingInfo bindingInfo = layoutInfo.second;
            ShaderResourceVisibility visibility;
            for (const auto& bindInfo : bindingInfo.m_resourcesRegisterMap)
            {
                visibility.m_descriptorTableShaderStageMask |= bindInfo.second.m_shaderStageMask;
            }
            AddShaderResourceVisibility(visibility);
        }

        return ResultCode::Success;
    }

    size_t PipelineLayoutDescriptor::GetHashInternal(size_t seed) const
    {
        eastl::hash_combine_raw(seed, m_rootConstantBinding.GetHash());
        for (const auto& visibility : m_shaderResourceVisibilities)
        {
            eastl::hash_combine_raw(seed, visibility.GetHash());
        }
        return seed;
    }
}