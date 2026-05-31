/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

 /*
 * Modified by SparkEngine in 2025
 *  -- Remove PipelineLibraryData temporarily
 */

#include "PipelineLibrary.h"

#include <Log/ILogSystem.h>

namespace Spark::RHI
{
    bool PipelineLibrary::ValidateIsInitialized() const
    {
        if (Validation::isEnabled)
        {
            if (!IsInitialized())
            {
                LOG_ERROR("[PipelineLibrary] PipelineLibrary is not initialized. This operation is only permitted on an initialized library.");
                return false;
            }
        }
        return true;
    }

    ResultCode PipelineLibrary::Init(Device& device, const PipelineLibraryDescriptor& descriptor)
    {
        if (IsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        ResultCode resultCode = InitInternal(device, descriptor);
        if (resultCode == ResultCode::Success)
        {
            DeviceObject::Init(device);
        }
        return resultCode;
    }


    void PipelineLibrary::Shutdown()
    {
        if (IsInitialized())
        {
            ShutdownInternal();
            DeviceObject::Shutdown();
        }
    }
}