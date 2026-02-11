/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/vector.h>
#include <EASTL/span.h>
#include <EASTL/array.h>

#include <RHI/Pipeline/ShaderStageFunction.h>

namespace Spark::RHI::DX12
{
    using ShaderByteCode = eastl::vector<uint8_t>;
    using ShaderByteCodeView = eastl::span<const uint8_t>;

    static const uint32_t ShaderSubStageCountMax = 2;

    class ShaderStageFunction final : public RHI::ShaderStageFunction
    {
    public:
        /// Assigns byte code to the function.
        void SetByteCode(uint32_t subStageIndex, const eastl::vector<uint8_t>& byteCode);

        /// Returns the assigned byte code.
        ShaderByteCodeView GetByteCode(uint32_t subStageIndex = 0) const;

        bool UseSpecializationConstants(uint32_t subStageIndex = 0) const;

    private:
        ShaderStageFunction() = default;
        ShaderStageFunction(RHI::ShaderStage shaderStage);

        ///////////////////////////////////////////////////////////////////
        // RHI::ShaderStageFunction
        RHI::ResultCode FinalizeInternal() override;
        ///////////////////////////////////////////////////////////////////

        eastl::array<ShaderByteCode, ShaderSubStageCountMax> m_byteCodes;
        //AZStd::array<SpecializationOffsets, ShaderSubStageCountMax> m_specializationOffsets;
    };
}