/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "Fence.h"

#include <Log/SpdLogSystem.h>

#include <RHI/Base.h>

namespace Spark::RHI
{
    bool Fence::ValidateIsInitialized() const
    {
        if (Validation::isEnabled)
        {
            if (!IsInitialized())
            {
                LOG_ERROR("[Fence] Fence is not initialized!");
                return false;
            }
        }

        return true;
    }

    ResultCode Fence::Init(Device& device, FenceState initialState)
    {
        if (Validation::isEnabled)
        {
            if (IsInitialized())
            {
                LOG_ERROR("[Fence] Fence is already initialized!");
                return ResultCode::InvalidOperation;
            }
        }

        const ResultCode resultCode = InitInternal(device, initialState);

        if (resultCode == ResultCode::Success)
        {
            DeviceObject::Init(device);
        }
        else
        {
            ASSERT(false, "Failed to create a fence");
        }

        return resultCode;
    }

    void Fence::Shutdown()
    {
        if (IsInitialized())
        {
            if (m_waitThread.joinable())
            {
                m_waitThread.join();
            }

            ShutdownInternal();
            DeviceObject::Shutdown();
        }
    }

    ResultCode Fence::SignalOnCpu()
    {
        if (!ValidateIsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        SignalOnCpuInternal();
        return ResultCode::Success;
    }

    ResultCode Fence::WaitOnCpu() const
    {
        if (!ValidateIsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        WaitOnCpuInternal();
        return ResultCode::Success;
    }

    ResultCode Fence::WaitOnCpuAsync(SignalCallback callback)
    {
        if (!ValidateIsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        if (!callback)
        {
            LOG_ERROR("Fence Callback is null.");
            return ResultCode::InvalidOperation;
        }

        if (m_waitThread.joinable())
        {
            m_waitThread.join();
        }

        m_waitThread = std::thread([this, callback]()
        {
            ResultCode resultCode = WaitOnCpu();
            if (resultCode != ResultCode::Success)
            {
                LOG_ERROR("Fence Failed to call WaitOnCpu in async thread.");
            }
            callback();
        });

        return ResultCode::Success;
    }

    ResultCode Fence::Reset()
    {
        if (!ValidateIsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        ResetInternal();
        return ResultCode::Success;
    }

    uint64_t Fence::Increment()
    {
        if (!ValidateIsInitialized())
        {
            return 0;
        }

        return IncrementInternal();
    }

    uint64_t Fence::GetPendingValue() const
    {
        if (!ValidateIsInitialized())
        {
            return 0;
        }

        return GetPendingValueInternal();
    }

    uint64_t Fence::GetCompletedValue() const
    {
        if (!ValidateIsInitialized())
        {
            return 0;
        }

        return GetCompletedValueInternal();
    }

    void Fence::SetPendingValue(uint64_t value)
    {
        if (!ValidateIsInitialized())
        {
            return;
        }

        SetPendingValueInternal(value);
    }

    FenceState Fence::GetFenceState() const
    {
        if (!ValidateIsInitialized())
        {
            return FenceState::Reset;
        }

        return GetFenceStateInternal();
    }
}