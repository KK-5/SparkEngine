#include "ReservedBuffer.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <RHI/Factory.h>
#include <RHI/Component/EventComponent.h>
#include <RHI/Context/RHIEventContext.h>
#include <RHI/Device/Device.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferPool.h>

namespace Spark::Render
{
    ReservedBuffer::~ReservedBuffer() = default;

    void ReservedBuffer::Shutdown()
    {
        // The reserved buffer before its backings, and the pool last.
        m_buffer.reset();
        m_backings.clear();
        m_pendingRequests.clear();
        m_pool.reset();
        m_mappedPageCount = 0;
    }

    bool ReservedBuffer::Init(RHI::Device& device, const Descriptor& descriptor)
    {
        const uint64_t tileSize = device.GetLimits().m_reservedPageSizeInBytes;
        if (descriptor.m_pageSize == 0 || descriptor.m_pageSize % tileSize != 0)
        {
            LOG_ERROR("[ReservedBuffer] {}: page size {} is not a whole number of the device's {}-byte tiles.",
                      descriptor.m_name.GetCStr(), descriptor.m_pageSize, tileSize);
            return false;
        }
        m_pageSize = descriptor.m_pageSize;
        const uint64_t pageCount = (descriptor.m_capacity + m_pageSize - 1) / m_pageSize;

        auto* factory = Service<RHI::Factory>::Get();

        // A block per backing: each is placed, filling one.
        RHI::BufferPoolDescriptor poolDescriptor;
        poolDescriptor.m_bindFlags              = descriptor.m_bindFlags;
        poolDescriptor.m_blockSizeInBytes       = m_pageSize;
        poolDescriptor.m_minAllocationAlignment = m_pageSize;
        // Mapped on the graphics queue only.
        poolDescriptor.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        m_pool = factory->CreateBufferPool();
        m_pool->SetName(descriptor.m_name);
        if (m_pool->Init(device, poolDescriptor) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedBuffer] {}: buffer pool init failed.", descriptor.m_name.GetCStr());
            m_pool.reset();
            return false;
        }

        RHI::BufferDescriptor bufferDescriptor(descriptor.m_bindFlags, pageCount * m_pageSize);
        bufferDescriptor.m_reserved        = true;
        bufferDescriptor.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        m_buffer = factory->CreateBuffer();
        m_buffer->SetName(descriptor.m_name);
        RHI::BufferInitRequest request;
        request.m_buffer     = m_buffer.get();
        request.m_descriptor = bufferDescriptor;
        if (m_pool->InitBuffer(request) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ReservedBuffer] {}: reserved buffer of {} bytes init failed.",
                      descriptor.m_name.GetCStr(), bufferDescriptor.m_byteCount);
            m_buffer.reset();
            m_pool.reset();
            return false;
        }

        m_backings.resize(pageCount);
        m_pendingRequests.resize(pageCount, RHI::NullEvent);
        return true;
    }

    void ReservedBuffer::Update(eastl::span<const Suballocator::PageChange> changes)
    {
        auto& events = *RHI::RHIEventExecuteContext::Current();

        for (const Suballocator::PageChange& change : changes)
        {
            if (change.m_page >= m_backings.size())
            {
                LOG_ERROR("[ReservedBuffer] {}: page {} is past its {} pages.",
                          m_buffer->GetName().GetCStr(), change.m_page, m_backings.size());
                continue;
            }

            Ptr<RHI::Buffer>& backing = m_backings[change.m_page];
            if (change.m_occupied == (backing != nullptr))
            {
                continue;
            }

            // A page has two states, so a change with a request pending turns it back: the request
            // goes and the page is as the GPU has it.
            RHI::RHIEvent& pending = m_pendingRequests[change.m_page];
            if (events.Valid(pending))
            {
                if (change.m_occupied)
                {
                    // The unmapping never went out, so the page keeps its backing.
                    backing = eastl::move(events.Get<RHI::BufferTileMappingRequest>(pending).m_backing);
                    ++m_mappedPageCount;
                }
                else
                {
                    // Never mapped, the backing goes with the request.
                    backing.reset();
                    --m_mappedPageCount;
                }
                events.DestoryEntity(pending);
                pending = RHI::NullEvent;
                continue;
            }

            RHI::BufferTileMappingRequest request;
            request.m_buffer               = m_buffer;
            request.m_mapping.m_buffer     = m_buffer.get();
            request.m_mapping.m_byteOffset = uint64_t(change.m_page) * m_pageSize;
            request.m_mapping.m_byteCount  = m_pageSize;

            if (change.m_occupied)
            {
                Ptr<RHI::Buffer> fresh = Service<RHI::Factory>::Get()->CreateBuffer();
                fresh->SetName(m_buffer->GetName());
                RHI::BufferInitRequest initRequest;
                initRequest.m_buffer     = fresh.get();
                initRequest.m_descriptor = RHI::BufferDescriptor(RHI::BufferBindFlags::None, m_pageSize);
                initRequest.m_descriptor.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
                if (m_pool->InitBuffer(initRequest) != RHI::ResultCode::Success)
                {
                    LOG_ERROR("[ReservedBuffer] {}: no memory to back page {}.",
                              m_buffer->GetName().GetCStr(), change.m_page);
                    continue;
                }
                backing                           = eastl::move(fresh);
                request.m_mapping.m_backingBuffer = backing.get();
                request.m_backing                 = backing;
                ++m_mappedPageCount;
            }
            else
            {
                request.m_backing = eastl::move(backing);
                --m_mappedPageCount;
            }

            pending = events.CreateEntity();
            events.Add<RHI::BufferTileMappingRequest>(pending, eastl::move(request));
        }
    }
}
