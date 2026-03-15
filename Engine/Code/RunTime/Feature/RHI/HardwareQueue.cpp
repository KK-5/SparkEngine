/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "HardwareQueue.h"

namespace Spark::RHI
{
    HardwareQueueClassMask GetHardwareQueueClassMask(HardwareQueueClass hardwareQueueClass)
    {
        return static_cast<HardwareQueueClassMask>(BIT(static_cast<uint32_t>(hardwareQueueClass)));
    }

    HardwareQueueClass GetMostCapableHardwareQueue(HardwareQueueClassMask queueMask)
    {
        if (CheckBitsAny(queueMask, HardwareQueueClassMask::Graphics))
        {
            return HardwareQueueClass::Graphics;
        }
        else if (CheckBitsAny(queueMask, HardwareQueueClassMask::Compute))
        {
            return HardwareQueueClass::Compute;
        }
        else
        {
            return HardwareQueueClass::Copy;
        }
    }

    bool IsHardwareQueueMoreCapable(HardwareQueueClass queueA, HardwareQueueClass queueB)
    {
         return queueA < queueB;
    }
}