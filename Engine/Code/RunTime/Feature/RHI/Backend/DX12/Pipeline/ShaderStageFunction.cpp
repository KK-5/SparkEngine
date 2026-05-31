/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ShaderStageFunction.h"

#include <Log/ILogSystem.h>
#include <EASTLEX/hash.h>

namespace Spark::RHI::DX12
{
    ShaderStageFunction::ShaderStageFunction(RHI::ShaderStage shaderStage)
        : RHI::ShaderStageFunction(shaderStage)
    {}

    RHI::ResultCode ShaderStageFunction::FinalizeInternal()
    {
        auto byteCode = GetByteCode();
        if (byteCode.empty())
        {
            LOG_ERROR("[ShaderStageFunction] Finalizing shader stage function with empty bytecodes.");
            return RHI::ResultCode::InvalidArgument;
        }

        size_t hash = 0;
        eastl::hash_combine(hash, byteCode.data(), byteCode.size());
        SetHash(hash);
        return RHI::ResultCode::Success;
    }
}