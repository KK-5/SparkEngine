/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/array.h>
#include <EASTL/numeric_limits.h>

#include <RHI/Pipeline/PipelineLayoutDescriptor.h>

namespace Spark::RHI::DX12
{
    using RootParameterIndex = uint16_t;

    static const RootParameterIndex InvalidRootParameterIndex = static_cast<uint16_t>(eastl::numeric_limits<uint16_t>::max());

    /**
     * Describes the root parameter binding for each component of a DX12 shader resource group.
     */
    class RootParameterBinding
    {
    public:
        /// The root index of the SRG root constant buffer (if it exists).
        RootParameterIndex m_constantBuffer;

        /// The root index of the SRG resource descriptor table (if it exists).
        RootParameterIndex m_resourceTable;

        /// The root indices of the SRG unbounded array resource descriptor tables (if any).
        /// TODO: This restriction should be lifted
        static const uint32_t MaxUnboundedArrays = 2;
        RootParameterIndex m_unboundedArrayResourceTables[MaxUnboundedArrays];

        /// If unbounded arrays are present, the bindless parameter index refers to the root argument designated for the bindless table
        RootParameterIndex m_bindlessTable;

        /// The root index of the SRG sampler descriptor table (if it exists).
        RootParameterIndex m_samplerTable;
    };

    /**
     * Describes root constant binding information.
     */
    struct RootConstantBinding
    {
        RootConstantBinding() = default;
        RootConstantBinding(
            uint32_t constantCount,
            uint32_t constantRegister,
            uint32_t constantRegisterSpace
        );

        size_t GetHash(size_t seed = 0) const;

        uint32_t m_constantCount = 0;
        uint32_t m_constantRegister = 0;
        uint32_t m_constantRegisterSpace = 0;
    };

    /**
     * Describes the shader stage mask for the
     * descriptor table used by the SRG.
     */
    struct ShaderResourceVisibility
    {
        ShaderResourceVisibility() = default;

        size_t GetHash(size_t seed = 0) const;

        RHI::ShaderStageMask m_descriptorTableShaderStageMask = RHI::ShaderStageMask::None;
    };

    class PipelineLayoutDescriptor final
        : public RHI::PipelineLayoutDescriptor
    {
        using Base = RHI::PipelineLayoutDescriptor;
    public:
        void SetRootConstantBinding(const RootConstantBinding& rootConstantBinding);

        const RootConstantBinding& GetRootConstantBinding() const;

        void AddShaderResourceVisibility(const ShaderResourceVisibility& shaderResourceVisibility);

        const ShaderResourceVisibility& GetShaderResourceVisibility(uint32_t index) const;

    private:
        PipelineLayoutDescriptor() = default;

        friend class ID3D12Factory;

        //////////////////////////////////////////////////////////////////////////
        /// PipelineLayoutDescriptor
        size_t GetHashInternal(size_t seed) const override;
        ResultCode FinalizeInternal() override;
        //////////////////////////////////////////////////////////////////////////

        RootConstantBinding m_rootConstantBinding;
        eastl::fixed_vector<ShaderResourceVisibility, RHI::Limits::Pipeline::ShaderResourceCountMax> m_shaderResourceVisibilities;
    };
}