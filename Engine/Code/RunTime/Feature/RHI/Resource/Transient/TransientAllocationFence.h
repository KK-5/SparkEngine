#pragma once

#include <cstdint>

#include <EASTL/numeric_limits.h>

#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/HardwareQueue.h>

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
    //! m_stage carries the pipeline stage at this anchor so the pool can populate
    //! aliasing barriers without coupling to pass-attachment iteration. For Create
    //! it should be the first attachment's stage; for Discard, the last.
    struct TransientAllocationFence
    {
        TransientAllocationFence() = default;

        TransientAllocationFence(HardwareQueueClassMask pipelines, uint32_t timelinePosition)
            : m_pipelines(pipelines)
            , m_timelinePosition(timelinePosition)
        {}

        TransientAllocationFence(HardwareQueueClassMask pipelines, uint32_t timelinePosition, AttachmentStage stage)
            : m_pipelines(pipelines)
            , m_timelinePosition(timelinePosition)
            , m_stage(stage)
        {}

        HardwareQueueClassMask m_pipelines        = HardwareQueueClassMask::All;
        uint32_t               m_timelinePosition = 0; // half-open interval [alloc, discard)
        AttachmentStage        m_stage            = AttachmentStage::Any;
    };

    static constexpr uint32_t InvalidTimelinePosition = eastl::numeric_limits<uint32_t>::max();
}
