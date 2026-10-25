#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <Base.h>
#include <Object/ObjectName.h>
#include <RHI/Context/RHIEvent.h>
#include <RHI/Resource/Buffer/BufferBindFlags.h>

#include "Suballocator.h"

namespace Spark::RHI
{
    class Buffer;
    class BufferPool;
    class Device;
}

namespace Spark::Render
{
    //! A reserved buffer whose pages are backed only while something is on them: each page
    //! touched gets a backing buffer of its own mapped under it, each page emptied loses it.
    //! Pages are a whole number of the device's tiles.
    class ReservedBuffer
    {
    public:
        struct Descriptor
        {
            uint64_t             m_capacity  = 0;
            uint64_t             m_pageSize  = 0;
            RHI::BufferBindFlags m_bindFlags = RHI::BufferBindFlags::None;
            ObjectName           m_name;
        };

        ReservedBuffer() = default;
        ReservedBuffer(const ReservedBuffer&)            = delete;
        ReservedBuffer& operator=(const ReservedBuffer&) = delete;
        ~ReservedBuffer();

        bool Init(RHI::Device& device, const Descriptor& descriptor);
        //! Before the RHI shuts down. Requests not yet submitted keep what they map.
        void Shutdown();

        //! Backs the pages newly touched and drops the ones emptied, a BufferTileMappingRequest
        //! each, which the render graph submits before the frame's work. A page changed back before
        //! its request is submitted takes the request back.
        void Update(eastl::span<const Suballocator::PageChange> changes);

        RHI::Buffer* GetBuffer() const { return m_buffer.get(); }
        uint64_t     GetPageSize() const { return m_pageSize; }
        uint32_t     GetMappedPageCount() const { return m_mappedPageCount; }

    private:
        Ptr<RHI::BufferPool> m_pool;
        uint64_t             m_pageSize = 0;

        // Declared before the reserved buffer, so they go after it.
        eastl::vector<Ptr<RHI::Buffer>> m_backings;
        Ptr<RHI::Buffer>                m_buffer;

        // Per page, its request not yet submitted.
        eastl::vector<RHI::RHIEvent> m_pendingRequests;
        uint32_t                     m_mappedPageCount = 0;
    };
}
