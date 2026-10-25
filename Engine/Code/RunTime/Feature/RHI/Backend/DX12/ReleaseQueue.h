/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Object/ObjectCollector.h>

#include "DX12.h"

namespace D3D12MA
{
    class Allocation;
}

namespace Spark::RHI::DX12
{
    /**
     * This is a deferred-release queue for ID3D12Object instances. Any DX12
     * object that needs to be released on the CPU timeline should be queued
     * here to ensure that a reference is held until the GPU has flushed the
     * the last frame using it.
     *
     * Each device has an object queue, and will synchronize its collect latency
     * to match the maximum number of frames allowed on the device.
     */
    class ReleaseQueueTraits
        : public ObjectCollectorTraits
    {
    public:
        using MutexType = std::mutex;
        using ObjectType = ID3D12Object;
    };

    using D3D12ObjReleaseQueue = ObjectCollector<ReleaseQueueTraits>;

    //! This is a deferred-release queue for allocations made through the 
    //! AMD D3D12MA library.
    //! Any D3D12MA allocation that needs to be released on the CPU timeline 
    //! should be queued here to ensure that a reference is held until the 
    //! GPU has flushed the the last frame using it.
    //! Each device has an object queue, and will synchronize its collect latency
    //! to match the maximum number of frames allowed on the device.
    class D3d12maReleaseQueueTraits
        : public ObjectCollectorTraits
    {
    public:
        using MutexType = std::mutex;
        using ObjectType = D3D12MA::Allocation;
    };

    using D3D12MAReleaseQueue = ObjectCollector<D3d12maReleaseQueueTraits>;
}
