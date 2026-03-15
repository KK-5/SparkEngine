/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/functional.h>
#include <thread>

#include <Math/Bit.h>
#include <RHI/Device/DeviceObject.h>

namespace Spark::RHI
{
    enum class FenceState : uint32_t
    {
        Reset = 0,
        Signaled
    };

    /// Fence capability flags
    enum class FenceFlags : uint32_t
    {
        None = 0,
        /// Set this if the Fence is signalled on the CPU and waited for on the device
        WaitOnDevice = BIT(0),
        /// Set this if the fence is signalled on one device and waited for on another device
        /// This is only supported if DeviceFeatures::m_crossDeviceFences is true for both devices
        CrossDevice = BIT(1),
    };
    DEFINE_ENUM_BITWISE_OPERATORS(FenceFlags, uint32_t);

    class Fence : public DeviceObject
    {
    public:
        virtual ~Fence() = default;

        /// Initializes the fence using the provided device and initial state.
        ResultCode Init(Device& device, FenceState initialState);

        /// Shuts down the fence.
        void Shutdown() override final;

        /// Signals the fence from the calling thread.
        RHI::ResultCode SignalOnCpu();

        /// Waits (blocks) for the fence on the calling thread.
        RHI::ResultCode WaitOnCpu() const;

        /// Resets the fence.
        RHI::ResultCode Reset();

        /// Returns whether the fence is signaled or not.
        FenceState GetFenceState() const;

        using SignalCallback = eastl::function<void()>;

        /// Spawns a dedicated thread to wait on the fence. The provided callback
        /// is invoked when the fence completes.
        ResultCode WaitOnCpuAsync(SignalCallback callback);

        /// BinaryFences in Vulkan need their dependent TimelineSemaphore Fences to be
        /// signalled. This is currently only implemented in Vulkan
        virtual void SetExternallySignalled(){};

    private:
        bool ValidateIsInitialized() const;

        //////////////////////////////////////////////////////////////////////////
        // Platform API

        /// Called when the fence is being initialized.
        virtual ResultCode InitInternal(Device& device, FenceState initialState) = 0;

        /// Called when the PSO is being shutdown.
        virtual void ShutdownInternal() = 0;

        /// Called when the fence is being signaled on the CPU.
        virtual void SignalOnCpuInternal() = 0;

        /// Called when the fence is waiting on the CPU.
        virtual void WaitOnCpuInternal() const = 0;

        /// Called when the fence is being reset.
        virtual void ResetInternal() = 0;

        /// Called to retrieve the current fence state.
        virtual FenceState GetFenceStateInternal() const = 0;

        //////////////////////////////////////////////////////////////////////////

        std::thread m_waitThread;
    };
}