#pragma once

#include <EASTL/functional.h>
#include <EASTL/heap.h>
#include <EASTL/vector.h>

#include <Handle/HandlePool.h>

namespace Spark::Render
{
    //! Allocator of stable slots in the Tag array. Holds the id space and nothing else:
    //! a handle returns its slot from a destructor, which can run while any context is
    //! being torn down, so the GPU array those ids index stays in GlobalBuffer.
    template<typename Tag>
    class SlotPool final : public HandlePool<SlotPool<Tag>>
    {
    public:
        void Init(uint32_t capacity)
        {
            this->Reserve(capacity);
            m_freeIds.clear();
            m_nextFreshId = 0;
        }

        //! Lowest free slot first, so the live set packs toward 0 and the upload stays
        //! short. An empty handle means the pool is full; the caller reports that.
        SharedHandle<SlotPool<Tag>> Allocate()
        {
            if (!m_freeIds.empty())
            {
                eastl::pop_heap(m_freeIds.begin(), m_freeIds.end(), eastl::greater<uint32_t>());
                const uint32_t id = m_freeIds.back();
                m_freeIds.pop_back();
                return this->MakeHandle(id);
            }

            if (m_nextFreshId >= this->Capacity())
            {
                return SharedHandle<SlotPool<Tag>>{};
            }

            return this->MakeHandle(m_nextFreshId++);
        }

        //! High-water mark. Every live slot is below it, so it is also the upload length.
        uint32_t Bound() const { return m_nextFreshId; }

    private:
        void Free(uint32_t id) override
        {
            m_freeIds.push_back(id);
            eastl::push_heap(m_freeIds.begin(), m_freeIds.end(), eastl::greater<uint32_t>());
        }

        eastl::vector<uint32_t> m_freeIds;
        uint32_t                m_nextFreshId = 0;
    };

    //! A slot this entity owns: the last copy to go returns it, so the slot comes back
    //! whoever destroys the entity and whenever they do it.
    template<typename Tag>
    using SlotRef = SharedHandle<SlotPool<Tag>>;

    //! A slot someone else owns. Exists to notice that ownership ended, so it must not
    //! take part in it.
    template<typename Tag>
    using SlotWeakRef = WeakHandle<SlotPool<Tag>>;
}
