/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Math/Bit.h>

namespace Spark::RHI
{
    //! Describes the three major logical classes of GPU hardware queues. Each queue class is a superset
    //! of the next. Graphics can do everything, compute can do compute / copy, and copy can only do copy
    //! operations. Scopes can be assigned a queue class, which gives hints to the scheduler for async
    //! queue operations.
    enum class HardwareQueueClass : uint32_t
    {
        //! Supports graphics, compute, and copy operations.
        Graphics = 0,

        //! Supports compute and copy operations.
        Compute,

        //! Supports only copy operations.
        Copy,

        Count
    };

    const uint32_t HardwareQueueClassCount = static_cast<uint32_t>(HardwareQueueClass::Count);

    //! Describes hardware queues as a mask, where each bit represents the queue family.
    enum class HardwareQueueClassMask : uint32_t
    {
        None        = 0,
        Graphics    = BIT(static_cast<uint32_t>(HardwareQueueClass::Graphics)),
        Compute     = BIT(static_cast<uint32_t>(HardwareQueueClass::Compute)),
        Copy        = BIT(static_cast<uint32_t>(HardwareQueueClass::Copy)),
        All         = Graphics | Compute | Copy
    };

    DEFINE_ENUM_BITWISE_OPERATORS(Spark::RHI::HardwareQueueClassMask, uint32_t);

    //! Returns the hardware queue class mask bit associated with the enum value.
    HardwareQueueClassMask GetHardwareQueueClassMask(HardwareQueueClass hardwareQueueClass);

    //! Scans the bit mask and returns the most capable queue from the set.
    HardwareQueueClass GetMostCapableHardwareQueue(HardwareQueueClassMask queueMask);

    //! Returns whether the first queue is more capable than the second queue.
    bool IsHardwareQueueMoreCapable(HardwareQueueClass queueA, HardwareQueueClass queueB);
}