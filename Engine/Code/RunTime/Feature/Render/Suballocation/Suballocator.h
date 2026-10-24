#pragma once

#include <mutex>

#include <EASTL/vector.h>

#include <Base.h>
#include <Memory/TLSFAllocator.h>
#include <Object/Object.h>

namespace Spark::RHI
{
    class Device;
}

namespace Spark::Render
{
    class Suballocator;
    class SuballocationRelease;

    //! A part of a Suballocator's address space. Owning and move-only; going, it is handed back
    //! to the allocator once the GPU is done with it.
    class Suballocation
    {
    public:
        Suballocation() = default;
        Suballocation(Suballocation&& other) noexcept;
        Suballocation& operator=(Suballocation&& other) noexcept;
        Suballocation(const Suballocation&)            = delete;
        Suballocation& operator=(const Suballocation&) = delete;
        ~Suballocation();

        //! An empty one is what a full allocator gives, and what a moved-from one is.
        bool IsValid() const { return m_allocator != nullptr; }

        uint64_t GetOffset() const { return m_offset; }
        uint64_t GetSize() const { return m_size; }

        void Reset();

    private:
        friend class Suballocator;

        Suballocation(Suballocator* allocator, uint64_t offset, uint64_t size, uint32_t node);

        Ptr<Suballocator> m_allocator;
        uint64_t          m_offset = 0;
        uint64_t          m_size   = 0;
        uint32_t          m_node   = TLSFAllocator::kInvalidNode;
    };

    //! Takes each Suballocation back frameCountMax frames after it goes, through
    //! Device::QueueForRelease. Held by every Suballocation and pending release, so a release
    //! may run after the owner is gone: it touches only this object. Thread-safe.
    class Suballocator final : public Object
    {
    public:
        struct PageChange
        {
            uint32_t m_page     = 0;
            bool     m_occupied = false;
        };

        //! All Suballocations must be gone before the device shuts down. pageSize 0 counts no pages.
        Suballocator(RHI::Device& device, uint64_t capacity, uint64_t pageSize = 0);

        //! An array of one element size passes alignment 1: offsets stay multiples of it, and a
        //! freed element's hole fits the next, which the padding of a larger alignment would not.
        Suballocation Allocate(uint64_t size, uint64_t alignment = 1);

        //! Pages whose touched-ness changed since the last call, once each, with their state now.
        //! A page empties on hand back, not on release. Take after the owner's write queue.
        void TakePageChanges(eastl::vector<PageChange>& changes);

        //! Every live Suballocation lies in [0, extent).
        uint64_t GetExtent() const;

        uint64_t GetCapacity() const;

    private:
        friend class Suballocation;
        friend class SuballocationRelease;

        //! Queues the hand back.
        void ReleaseSuballocation(uint64_t offset, uint64_t size, uint32_t node);

        //! The hand back, frameCountMax frames later.
        void Free(uint32_t node, uint64_t offset, uint64_t size);

        //! Under the lock.
        void CountPages(uint64_t offset, uint64_t size, bool touch);

        RHI::Device* m_device   = nullptr;
        uint64_t     m_pageSize = 0;

        mutable std::mutex m_mutex;
        TLSFAllocator      m_allocator;

        //! Grown up to the highest page touched so far.
        eastl::vector<uint32_t> m_pageCounts;
        eastl::vector<uint8_t>  m_pageQueued;
        eastl::vector<uint32_t> m_changedPages;
    };
}
