#pragma once

#include <EASTL/functional.h>
#include <EASTL/heap.h>
#include <EASTL/vector.h>

#include <CoreComponents/Tags.h>
#include <ECS/BasicContext.h>
#include <Log/ILogSystem.h>

#include <Binding/StagedArrayBuffer.h>


namespace Spark::Render
{
    //! A complete reference to one slot of the Tag array: which array, which index,
    //! and which occupancy of that index. Allocated when the entity first carries every
    //! Source, invalidated on release, and unchanged in between — so a consumer may copy
    //! it and bake m_id once instead of resolving anything per frame.
    //!
    //! Self-describing on purpose: a copy answers IsValid() on its own, so a consumer
    //! needs no handle to the buffer's owner. m_version is what makes that safe — ids are
    //! recycled, and an id alone cannot tell "still mine" from "someone else's now".
    //!
    //! Points at the container, not its data, so a future capacity growth reallocating
    //! the array does not dangle every outstanding copy. The container's lifetime is the
    //! GlobalBuffer's; teardown order must reap consumers first (for Instances, that is
    //! RenderSystem::ShutdownInternal running the router before the binding system).
    template<typename Tag>
    struct GlobalBufferSlotRef
    {
        const eastl::vector<uint32_t>* m_versions = nullptr;
        uint32_t                       m_id       = UINT32_MAX;
        uint32_t                       m_version  = 0;

        bool IsValid() const
        {
            return m_versions && m_id < m_versions->size() && (*m_versions)[m_id] == m_version;
        }
    };

    //! A StagedArrayBuffer addressed by STABLE SLOT: this layer owns the allocator and
    //! the occupancy versions, and stamps each source entity with its GlobalBufferSlotRef.
    //! Sources are the components an entity must carry to own a slot, Element is the GPU
    //! record.
    //!
    //! Worth it only when something ELSE stores the index and needs it to outlive the
    //! frame — a baked StartInstanceLocation (g_Instances), or an index living in another
    //! GPU array (InstanceData::m_materialIndex -> g_Materials). An array the shader just
    //! iterates gains nothing here and pays for the holes: use StagedArrayBuffer directly
    //! and pack densely, as g_Lights does.
    //!
    //! Because slots are stable the array has holes, so the upload spans [0, Size()) and
    //! the holes are copied bytes nobody indexes. They are NOT cleared — see
    //! TODO_GlobalBufferUploadPlan.md §6.
    template<typename Tag, typename Element, typename... Sources>
    class GlobalBuffer
    {
    public:
        using Descriptor = typename StagedArrayBuffer<Element>::Descriptor;

        void Init(RHI::RHIContext& rhiCtx, const Descriptor& descriptor)
        {
            m_array.Init(rhiCtx, descriptor);
            m_versions.assign(descriptor.m_capacity, 0);
        }

        //! Slots and allocator must clear together — a leftover slot would index into a
        //! fresh allocator after re-init. Emptying m_versions also makes every copy still
        //! held elsewhere answer IsValid() == false rather than read a stale occupancy.
        template<typename Ctx>
        void Shutdown(Ctx& ctx, RHI::RHIContext& rhiCtx)
        {
            ctx.template Clear<GlobalBufferSlotRef<Tag>>();
            m_versions.clear();
            m_freeIds.clear();
            m_nextFreshId = 0;

            m_array.Shutdown(rhiCtx);
        }

        //! process: void(Entity, Element&, const Sources&...)
        template<typename Ctx, typename ProcessFn>
        void Update(Ctx& ctx, RHI::RHIContext& rhiCtx, uint32_t frameIndex, ProcessFn&& process)
        {
            using Slot = GlobalBufferSlotRef<Tag>;

            // Nothing is allocated or encoded until the buffer materializes, so the first
            // successful frame still carries every entity: the mirror is complete by
            // construction, not by replaying the warmup frames.
            if (!m_array.BindFrame(rhiCtx, frameIndex))
            {
                return;
            }

            // The version bump inside FreeId invalidates this very component, which is
            // also what stops a DeadTag entity that outlives one frame from returning its
            // id twice.
            ctx.template GetView<Slot, DeadTag>().each([&](auto, const Slot& slot)
            {
                if (!slot.IsValid())
                {
                    return;
                }
                FreeId(slot.m_id);
            });

            // Structural write inside iteration: the added component is only in the
            // exclude set, never an iterated pool. First thing to route through a deferred
            // command buffer once this step goes parallel.
            ctx.template GetView<Sources...>(Exclude<Slot, DeadTag>).each(
                [&](auto entity, const Sources&...)
            {
                uint32_t id = 0;
                if (!AllocateId(id))
                {
                    return;
                }
                ctx.template Add<Slot>(entity, Slot{ &m_versions, id, m_versions[id] });
            });

            ctx.template GetView<Slot, Sources...>(Exclude<DeadTag>).each(
                [&](auto entity, const Slot& slot, const Sources&... sources)
            {
                process(entity, m_array[slot.m_id], sources...);
            });

            m_array.Upload(rhiCtx, m_nextFreshId);
        }

        //! High-water mark. Every live slot is below it, so it is also the upload length.
        uint32_t Size() const { return m_nextFreshId; }

    private:
        //! Lowest free id first, so the live set packs toward 0 and the upload stays short.
        bool AllocateId(uint32_t& out)
        {
            if (!m_freeIds.empty())
            {
                eastl::pop_heap(m_freeIds.begin(), m_freeIds.end(), eastl::greater<uint32_t>());
                out = m_freeIds.back();
                m_freeIds.pop_back();
                return true;
            }

            if (m_nextFreshId >= m_array.Capacity())
            {
                LOG_ERROR("[GlobalBuffer] '{}' capacity={} overflow; dropping entity.",
                          m_array.Name().GetCStr(), m_array.Capacity());
                return false;
            }

            out = m_nextFreshId++;
            return true;
        }

        //! Bumping first is what severs every outstanding reference to this occupancy,
        //! including the ones a consumer copied — the id is about to name someone else.
        void FreeId(uint32_t id)
        {
            ++m_versions[id];
            m_freeIds.push_back(id);
            eastl::push_heap(m_freeIds.begin(), m_freeIds.end(), eastl::greater<uint32_t>());
        }

        StagedArrayBuffer<Element> m_array;
        uint32_t                   m_nextFreshId = 0;
        eastl::vector<uint32_t>    m_freeIds;

        //! Occupancy counter per id, referenced by every GlobalBufferSlotRef handed out.
        //! Never reallocated while references are live (fixed at Init).
        eastl::vector<uint32_t>    m_versions;
    };
}
