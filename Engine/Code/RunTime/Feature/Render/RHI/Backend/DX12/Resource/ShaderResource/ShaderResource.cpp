/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ShaderResource.h"

namespace Spark::RHI::DX12
{
    const ShaderResourceCompiledData& ShaderResource::GetCompiledData() const
    {
        return m_compiledData[m_compiledDataIndex];
    }
}