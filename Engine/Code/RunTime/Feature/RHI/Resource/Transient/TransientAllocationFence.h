#pragma once

#include <cstdint>

#include <EASTL/numeric_limits.h>

#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/HardwareQueue.h>
#include <RHI/Resource/AccessFlags.h>

namespace Spark::RHI
{
    //! A timeline anchor passed to TransientResourcePool::Create*() and Discard().
    //!
    //! m_timelinePosition is an opaque, monotonically-increasing integer assigned by
    //! the caller. The RHI does not interpret it — it is purely an ordering key used
    //! to decide whether two resources' lifetimes overlap and therefore whether they
    //! can share heap memory.
    //!
    //! m_pipelines describes which hardware queues participate at this anchor. When
    //! a transient resource is consumed across multiple queues the pool unions the
    //! Create / Discard masks to compute the full set of pipelines that must
    //! synchronize before the memory range is recycled.
    //!
    //! m_queue / m_stage / m_access describe the use at this anchor so the pool can seed
    //! an aliased resource's initial state without coupling to pass-attachment iteration:
    //! for Create the first use (only its queue is read), for Discard the last.
    struct TransientAllocationFence
    {
        TransientAllocationFence() = default;

        TransientAllocationFence(HardwareQueueClassMask pipelines, uint32_t timelinePosition)
            : m_pipelines(pipelines)
            , m_timelinePosition(timelinePosition)
        {}

        TransientAllocationFence(
            HardwareQueueClassMask pipelines, uint32_t timelinePosition,
            HardwareQueueClass queue, AttachmentStage stage, AccessFlags access)
            : m_pipelines(pipelines)
            , m_timelinePosition(timelinePosition)
            , m_queue(queue)
            , m_stage(stage)
            , m_access(access)
        {}

        HardwareQueueClassMask m_pipelines        = HardwareQueueClassMask::All;
        uint32_t               m_timelinePosition = 0; // half-open interval [alloc, discard)
        HardwareQueueClass     m_queue            = HardwareQueueClass::Graphics;
        AttachmentStage        m_stage            = AttachmentStage::Uninitialized;
        AccessFlags            m_access           = AccessFlags::None;
    };

    static constexpr uint32_t InvalidTimelinePosition = eastl::numeric_limits<uint32_t>::max();
}
