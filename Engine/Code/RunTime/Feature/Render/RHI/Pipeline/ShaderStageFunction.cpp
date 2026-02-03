/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ShaderStageFunction.h"

#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    ShaderStageFunction::ShaderStageFunction(ShaderStage shaderStage)
        : m_shaderStage{shaderStage}
    {}

    ShaderStage ShaderStageFunction::GetShaderStage() const
    {
        return m_shaderStage;
    }

    size_t ShaderStageFunction::GetHash() const
    {
        return m_hash;
    }

    void ShaderStageFunction::SetHash(size_t hash)
    {
        m_hash = hash;
    }

    ResultCode ShaderStageFunction::Finalize()
    {
        if (m_shaderStage == ShaderStage::Unknown)
        {
            LOG_ERROR("[ShaderStageFunction] The shader stage is Unknown. This is not valid.");
            return ResultCode::InvalidArgument;
        }

        // Reset hash so we can validate the platform actually built it.
        m_hash = 0 ;

        const ResultCode resultCode = FinalizeInternal();

        // Do post finalize validation if the platform claims it succeeded.
        if (resultCode == ResultCode::Success)
        {
            if (m_hash ==  0 )
            {
                LOG_ERROR("[ShaderStageFunction] The hash value was not assigned in the platform Finalize implementation.");
                return ResultCode::Fail;
            }
        }

        return resultCode;
    }
}