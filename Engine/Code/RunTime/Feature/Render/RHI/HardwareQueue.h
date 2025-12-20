#pragma once

#include <Math/Bit.h>

namespace Spark::RHI
{
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
}