#pragma once

#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <Base.h>
#include <Log/ILogSystem.h>
#include <Object/Object.h>

namespace Spark::Render
{
    template<typename Pool> class SharedHandle;
    template<typename Pool> class WeakHandle;

    //! Reference-counted allocator of bounded uint32 ids. The derived pool decides WHICH id
    //! to hand out; counting, occupancy generations and the moment of reclamation live here.
    //!
    //! The last SharedHandle to go returns the id, so reclamation depends on nobody
    //! observing a death at the right time: whoever destroys the holder, and whenever they
    //! do it, the id comes back.
    //!
    //! Refcounted itself, so a handle may outlive whoever owns the pool. In exchange it may
    //! hold only CPU state -- Free() runs from a destructor, which can run while any context
    //! is being torn down.
    //!
    //! Counts are not atomic: allocation and release both happen on one thread.
    template<typename Pool>
    class HandlePool : public Object
    {
    protected:
        //! Fixes the id space. Drops every entry, so only before anything is handed out.
        void Reserve(uint32_t capacity)
        {
            m_entries.assign(capacity, Entry{});
        }

        uint32_t Capacity() const { return static_cast<uint32_t>(m_entries.size()); }

        //! Wraps an id the derived pool just allocated; the count starts at 1.
        SharedHandle<Pool> MakeHandle(uint32_t id);

        //! Called exactly once, when the last handle to id is gone. May only write the
        //! pool's own memory -- anything needing a context has to be queued and drained by
        //! the pool's own system.
        virtual void Free(uint32_t id) = 0;

    private:
        friend class SharedHandle<Pool>;
        friend class WeakHandle<Pool>;

        //! m_generation counts occupancies of this id. Bumping it is what tells a leftover
        //! WeakHandle that the id it names belongs to someone else now.
        struct Entry
        {
            uint32_t m_refCount   = 0;
            uint32_t m_generation = 0;
        };

        // Not AddRef/Release: those names are Object's, and redeclaring them here would
        // hide the pair intrusive_ptr calls.
        void AddHandleRef(uint32_t id)
        {
            ASSERT(id < m_entries.size(), "[HandlePool] Id {} is outside the pool.", id);
            ++m_entries[id].m_refCount;
        }

        void ReleaseHandleRef(uint32_t id)
        {
            ASSERT(id < m_entries.size(), "[HandlePool] Id {} is outside the pool.", id);
            Entry& entry = m_entries[id];
            ASSERT(entry.m_refCount > 0, "[HandlePool] Released id {}, which nobody held.", id);

            if (--entry.m_refCount == 0)
            {
                // Severed before reclaimed: Free may reach code still holding a WeakHandle.
                ++entry.m_generation;
                Free(id);
            }
        }

        uint32_t GenerationOf(uint32_t id) const
        {
            ASSERT(id < m_entries.size(), "[HandlePool] Id {} is outside the pool.", id);
            return m_entries[id].m_generation;
        }

        bool IsLive(uint32_t id, uint32_t generation) const
        {
            return id < m_entries.size()
                && m_entries[id].m_refCount > 0
                && m_entries[id].m_generation == generation;
        }

        eastl::vector<Entry> m_entries;
    };

    //! Owning handle. A component member holds one, and its destruction returns the id --
    //! which is why it works the same whether the component was removed, its entity
    //! destroyed, or its whole context cleared.
    template<typename Pool>
    class SharedHandle
    {
    public:
        SharedHandle() = default;

        SharedHandle(const SharedHandle& other)
            : m_pool(other.m_pool)
            , m_id(other.m_id)
        {
            if (m_pool)
            {
                m_pool->AddHandleRef(m_id);
            }
        }

        SharedHandle(SharedHandle&& other) noexcept
            : m_pool(eastl::move(other.m_pool))
            , m_id(other.m_id)
        {
            other.m_id = 0;
        }

        ~SharedHandle()
        {
            Reset();
        }

        SharedHandle& operator=(SharedHandle other) noexcept
        {
            Swap(other);
            return *this;
        }

        //! Whether this handle holds an id at all. An empty one answers false.
        bool IsValid() const { return m_pool != nullptr; }

        //! Only meaningful while IsValid().
        uint32_t Get() const { return m_id; }

        WeakHandle<Pool> Weak() const;

        void Reset()
        {
            if (m_pool)
            {
                // m_pool keeps the pool alive across the release, which may reclaim.
                m_pool->ReleaseHandleRef(m_id);
                m_pool.reset();
                m_id = 0;
            }
        }

        void Swap(SharedHandle& other)
        {
            m_pool.swap(other.m_pool);
            eastl::swap(m_id, other.m_id);
        }

    private:
        friend class HandlePool<Pool>;

        SharedHandle(Pool* pool, uint32_t id)
            : m_pool(pool)
            , m_id(id)
        {
        }

        Ptr<Pool> m_pool;
        uint32_t  m_id = 0;
    };

    //! Non-owning handle. What an observer holds: it exists to notice that ownership ended,
    //! so it must not take part in ownership.
    template<typename Pool>
    class WeakHandle
    {
    public:
        WeakHandle() = default;

        //! False from the moment the last SharedHandle goes, and false again once the id
        //! has been handed to someone else.
        bool IsValid() const { return m_pool && m_pool->IsLive(m_id, m_generation); }

        //! Only meaningful while IsValid().
        uint32_t Get() const { return m_id; }

    private:
        friend class SharedHandle<Pool>;

        WeakHandle(Pool* pool, uint32_t id, uint32_t generation)
            : m_pool(pool)
            , m_id(id)
            , m_generation(generation)
        {
        }

        //! Keeps the pool alive, not the id -- the reason this can answer IsValid() on its
        //! own rather than borrowing a pointer into the pool's tables.
        Ptr<Pool> m_pool;
        uint32_t  m_id         = 0;
        uint32_t  m_generation = 0;
    };

    template<typename Pool>
    SharedHandle<Pool> HandlePool<Pool>::MakeHandle(uint32_t id)
    {
        ASSERT(id < m_entries.size(), "[HandlePool] Id {} is outside the pool.", id);
        ASSERT(m_entries[id].m_refCount == 0, "[HandlePool] Id {} was handed out twice.", id);

        m_entries[id].m_refCount = 1;
        return SharedHandle<Pool>(static_cast<Pool*>(this), id);
    }

    template<typename Pool>
    WeakHandle<Pool> SharedHandle<Pool>::Weak() const
    {
        return m_pool != nullptr
            ? WeakHandle<Pool>(m_pool.get(), m_id, m_pool->GenerationOf(m_id))
            : WeakHandle<Pool>{};
    }
}
