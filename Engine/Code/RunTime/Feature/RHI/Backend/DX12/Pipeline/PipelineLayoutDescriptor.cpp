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
#include <Log/ILogSystem.h>
#include <Math/Interval.h>

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

    void PipelineLayoutDescriptor::SetRootConstantBinding(const RootConstantBinding& rootConstantBinding)
    {
        m_rootConstantBinding = rootConstantBinding;
    }

    const RootConstantBinding& PipelineLayoutDescriptor::GetRootConstantBinding() const
    {
        return m_rootConstantBinding;
    }

    void PipelineLayoutDescriptor::ValidateShaderInputOverlapInternal(
        const RHI::ShaderInputHandle& newHandle,
        const RHI::ShaderInputHandle& existingHandle,
        uint32_t spaceId) const
    {
        // Two constants sharing the same register are fields of the same cbuffer — check byte
        // range overlap instead of register overlap. Different registers = different cbuffers,
        // no overlap possible at the CBV level.
        if (newHandle.m_type == RHI::ShaderInputType::Constant &&
            existingHandle.m_type == RHI::ShaderInputType::Constant)
        {
            const auto& newDesc      = GetConstantDescriptor(newHandle.m_index);
            const auto& existingDesc = GetConstantDescriptor(existingHandle.m_index);

            if (newDesc.m_registerId != existingDesc.m_registerId)
            {
                return;
            }

            const Interval newInterval(newDesc.m_constantByteOffset,
                newDesc.m_constantByteOffset + newDesc.m_constantByteCount - 1);
            const Interval existingInterval(existingDesc.m_constantByteOffset,
                existingDesc.m_constantByteOffset + existingDesc.m_constantByteCount - 1);
            if (newInterval.Overlaps(existingInterval))
            {
                LOG_ERROR("[PipelineLayoutDescriptor] space={}: constant byte range overlap in cbuffer b{}: "
                          "new [{}, {}], existing [{}, {}].",
                          spaceId, newDesc.m_registerId,
                          newInterval.m_min, newInterval.m_max,
                          existingInterval.m_min, existingInterval.m_max);
            }
            return;
        }

        // Maps each ShaderInput to a (HlslRegisterNamespace, baseRegister, count) triple.
        // DX12 has four disjoint HLSL register namespaces within a space (b/t/u/s).
        // Vulkan flattens these into a single binding namespace per descriptor set,
        // using VulkanBindingShift offsets to keep them non-overlapping.
        enum class HlslRegisterNamespace { CBV, SRV, UAV, Sampler };

        struct BindingRange
        {
            HlslRegisterNamespace m_ns;
            uint32_t              m_baseRegister;
            uint32_t              m_count;
        };

        auto GetRange = [this](const RHI::ShaderInputHandle& handle) -> BindingRange
        {
            switch (handle.m_type)
            {
            case RHI::ShaderInputType::Constant:
            {
                const auto& desc = GetConstantDescriptor(handle.m_index);
                return { HlslRegisterNamespace::CBV, desc.m_registerId, 1 };
            }
            case RHI::ShaderInputType::Buffer:
            {
                const auto& desc = GetBufferDescriptor(handle.m_index);
                HlslRegisterNamespace ns = HlslRegisterNamespace::SRV;
                if (desc.m_access == RHI::ShaderInputBufferAccess::Constant)
                {
                    ns = HlslRegisterNamespace::CBV;
                }
                else if (desc.m_access == RHI::ShaderInputBufferAccess::ReadWrite)
                {
                    ns = HlslRegisterNamespace::UAV;
                }
                return { ns, desc.m_registerId, desc.m_count };
            }
            case RHI::ShaderInputType::Image:
            {
                const auto& desc = GetImageDescriptor(handle.m_index);
                HlslRegisterNamespace ns = (desc.m_access == RHI::ShaderInputImageAccess::ReadWrite)
                    ? HlslRegisterNamespace::UAV
                    : HlslRegisterNamespace::SRV;
                return { ns, desc.m_registerId, desc.m_count };
            }
            case RHI::ShaderInputType::Sampler:
            {
                const auto& desc = GetSamplerDescriptor(handle.m_index);
                return { HlslRegisterNamespace::Sampler, desc.m_registerId, desc.m_count };
            }
            default:
                return { HlslRegisterNamespace::SRV, 0, 0 };
            }
        };

        const BindingRange newRange      = GetRange(newHandle);
        const BindingRange existingRange = GetRange(existingHandle);

        if (newRange.m_count == 0 || existingRange.m_count == 0)
        {
            return;
        }

        // DX12 has four disjoint register namespaces (b/t/u/s) within a space,
        // so only inputs sharing the same namespace can overlap.
        if (newRange.m_ns != existingRange.m_ns)
        {
            return;
        }

        const Interval newInterval(newRange.m_baseRegister, newRange.m_baseRegister + newRange.m_count - 1);
        const Interval existingInterval(existingRange.m_baseRegister, existingRange.m_baseRegister + existingRange.m_count - 1);
        if (newInterval.Overlaps(existingInterval))
        {
            LOG_ERROR("[PipelineLayoutDescriptor] space={}: DX12 register overlap detected. "
                      "New binding [{}, {}], existing [{}, {}].",
                      spaceId,
                      newInterval.m_min, newInterval.m_max,
                      existingInterval.m_min, existingInterval.m_max);
        }
    }

    size_t PipelineLayoutDescriptor::GetHashInternal(size_t seed) const
    {
        eastl::hash_combine_raw(seed, m_rootConstantBinding.GetHash());
        return seed;
    }
}