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

    class PipelineLayoutDescriptor final
        : public RHI::PipelineLayoutDescriptor
    {
        using Base = RHI::PipelineLayoutDescriptor;
    public:
        void SetRootConstantBinding(const RootConstantBinding& rootConstantBinding);

        const RootConstantBinding& GetRootConstantBinding() const;

    private:
        PipelineLayoutDescriptor() = default;

        friend class ID3D12Factory;

        //////////////////////////////////////////////////////////////////////////
        /// PipelineLayoutDescriptor
        size_t GetHashInternal(size_t seed) const override;
        void ValidateShaderInputOverlapInternal(
            const ShaderInputHandle& newHandle,
            const ShaderInputHandle& existingHandle,
            uint32_t spaceId) const override;
        //////////////////////////////////////////////////////////////////////////

        RootConstantBinding m_rootConstantBinding;
    };
}