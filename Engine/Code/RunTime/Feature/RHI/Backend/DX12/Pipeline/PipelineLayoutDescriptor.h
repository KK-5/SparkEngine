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

    class PipelineLayoutDescriptor final
        : public RHI::PipelineLayoutDescriptor
    {
        using Base = RHI::PipelineLayoutDescriptor;
    private:
        PipelineLayoutDescriptor() = default;

        friend class ID3D12Factory;

        //////////////////////////////////////////////////////////////////////////
        /// PipelineLayoutDescriptor
        void ValidateShaderInputOverlapInternal(
            const ShaderInputHandle& newHandle,
            const ShaderInputHandle& existingHandle,
            uint32_t spaceId) const override;
        //////////////////////////////////////////////////////////////////////////
    };
}