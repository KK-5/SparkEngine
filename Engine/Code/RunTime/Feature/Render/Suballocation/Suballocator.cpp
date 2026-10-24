#include "Suballocator.h"

#include <RHI/Device/Device.h>

namespace Spark::Render
{
    //! Waits out the GPU in the device's release queue, then hands the part back. Holds the
    //! allocator, so the allocator lives as long as any of its parts is pending.
    class SuballocationRelease final : public Object
    {
    public:
        SuballocationRelease(Suballocator* allocator, uint32_t node, uint64_t offset, uint64_t size)
            : m_allocator(allocator)
            , m_node(node)
            , m_offset(offset)
            , m_size(size)
        {
        }

        ~SuballocationRelease() override
        {
            m_allocator->Free(m_node, m_offset, m_size);
        }

    private:
        Ptr<Suballocator> m_allocator;
        uint32_t          m_node;
        uint64_t          m_offset;
        uint64_t          m_size;
    };

    Suballocation::Suballocation(Suballocator* allocator, uint64_t offset, uint64_t size, uint32_t node)
        : m_allocator(allocator)
        , m_offset(offset)
        , m_size(size)
        , m_node(node)
    {
    }

    Suballocation::Suballocation(Suballocation&& other) noexcept
        : m_allocator(eastl::move(other.m_allocator))
        , m_offset(other.m_offset)
        , m_size(other.m_size)
        , m_node(other.m_node)
    {
        other.m_offset = 0;
        other.m_size   = 0;
        other.m_node   = TLSFAllocator::kInvalidNode;
    }

    Suballocation& Suballocation::operator=(Suballocation&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_allocator = eastl::move(other.m_allocator);
            m_offset    = other.m_offset;
            m_size      = other.m_size;
            m_node      = other.m_node;
            other.m_offset = 0;
            other.m_size   = 0;
            other.m_node   = TLSFAllocator::kInvalidNode;
        }
        return *this;
    }

    Suballocation::~Suballocation()
    {
        Reset();
    }

    void Suballocation::Reset()
    {
        if (!m_allocator)
        {
            return;
        }
        // Moved out first: the allocator may be held by nothing else, and the release it queues
        // keeps it alive from here on.
        Ptr<Suballocator> allocator = eastl::move(m_allocator);
        allocator->ReleaseSuballocation(m_offset, m_size, m_node);
        m_offset = 0;
        m_size   = 0;
        m_node   = TLSFAllocator::kInvalidNode;
    }

    Suballocator::Suballocator(RHI::Device& device, uint64_t capacity, uint64_t pageSize)
        : m_device(&device)
        , m_pageSize(pageSize)
    {
        m_allocator.Init(capacity);
    }

    Suballocation Suballocator::Allocate(uint64_t size, uint64_t alignment)
    {
        TLSFAllocator::Allocation allocation;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            allocation = m_allocator.Allocate(size, alignment);
            if (allocation.IsValid())
            {
                CountPages(allocation.m_offset, size, true);
            }
        }
        if (!allocation.IsValid())
        {
            return {};
        }
        return Suballocation(this, allocation.m_offset, size, allocation.m_node);
    }

    void Suballocator::TakePageChanges(eastl::vector<PageChange>& changes)
    {
        changes.clear();
        std::lock_guard<std::mutex> lock(m_mutex);
        for (uint32_t page : m_changedPages)
        {
            changes.push_back(PageChange{ page, m_pageCounts[page] > 0 });
            m_pageQueued[page] = 0;
        }
        m_changedPages.clear();
    }

    uint64_t Suballocator::GetExtent() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_allocator.GetExtent();
    }

    uint64_t Suballocator::GetCapacity() const
    {
        return m_allocator.GetCapacity();
    }

    void Suballocator::ReleaseSuballocation(uint64_t offset, uint64_t size, uint32_t node)
    {
        m_device->QueueForRelease(Ptr<Object>(new SuballocationRelease(this, node, offset, size)));
    }

    void Suballocator::Free(uint32_t node, uint64_t offset, uint64_t size)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_allocator.Free(node);
        CountPages(offset, size, false);
    }

    void Suballocator::CountPages(uint64_t offset, uint64_t size, bool touch)
    {
        if (m_pageSize == 0)
        {
            return;
        }

        // A part straddling a page boundary counts on both pages.
        const uint32_t first = static_cast<uint32_t>(offset / m_pageSize);
        const uint32_t last  = static_cast<uint32_t>((offset + size - 1) / m_pageSize);
        if (last >= m_pageCounts.size())
        {
            m_pageCounts.resize(last + 1, 0);
            m_pageQueued.resize(last + 1, 0);
        }

        for (uint32_t page = first; page <= last; ++page)
        {
            uint32_t& count = m_pageCounts[page];
            count = touch ? count + 1 : count - 1;
            const bool edge = touch ? count == 1 : count == 0;
            if (edge && !m_pageQueued[page])
            {
                m_pageQueued[page] = 1;
                m_changedPages.push_back(page);
            }
        }
    }
}
