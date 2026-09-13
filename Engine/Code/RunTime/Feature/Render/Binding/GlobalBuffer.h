#pragma once

#include <CoreComponents/Tags.h>
#include <ECS/BasicContext.h>
#include <Log/ILogSystem.h>

#include <Binding/SlotPool.h>
#include <Binding/StagedArrayBuffer.h>


namespace Spark::Render
{
    //! A StagedArrayBuffer addressed by STABLE SLOT: this layer owns a SlotPool and hands
    //! each source entity the slot it keeps. Sources are the components an entity must
    //! carry to own a slot, Element is the GPU record.
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
        using Slot       = SlotRef<Tag>;

        void Init(RHI::RHIContext& rhiCtx, const Descriptor& descriptor)
        {
            m_array.Init(rhiCtx, descriptor);

            m_slots = Ptr<SlotPool<Tag>>(new SlotPool<Tag>());
            m_slots->Init(descriptor.m_capacity);
        }

        //! Clearing the slots returns them; dropping the pool afterwards is what keeps a
        //! handle still held elsewhere off a re-inited id space — it keeps the old pool
        //! alive instead, and a re-Init builds a fresh one.
        template<typename Ctx>
        void Shutdown(Ctx& ctx, RHI::RHIContext& rhiCtx)
        {
            ctx.template Clear<Slot>();
            m_slots.reset();

            m_array.Shutdown(rhiCtx);
        }

        //! process: void(Entity, Element&, const Sources&...)
        template<typename Ctx, typename ProcessFn>
        void Update(Ctx& ctx, RHI::RHIContext& rhiCtx, uint32_t frameIndex, ProcessFn&& process)
        {
            // Nothing is allocated or encoded until the buffer materializes, so the first
            // successful frame still carries every entity: the mirror is complete by
            // construction, not by replaying the warmup frames.
            if (!m_array.BindFrame(rhiCtx, frameIndex))
            {
                return;
            }

            // Structural write inside iteration: the added component is only in the
            // exclude set, never an iterated pool. First thing to route through a deferred
            // command buffer once this step goes parallel.
            ctx.template GetView<Sources...>(Exclude<Slot, DeadTag>).each(
                [&](auto entity, const Sources&...)
            {
                Slot slot = m_slots->Allocate();
                if (!slot.IsValid())
                {
                    LOG_ERROR("[GlobalBuffer] '{}' capacity={} overflow; dropping entity.",
                              m_array.Name().GetCStr(), m_array.Capacity());
                    return;
                }
                ctx.template Add<Slot>(entity, eastl::move(slot));
            });

            ctx.template GetView<Slot, Sources...>(Exclude<DeadTag>).each(
                [&](auto entity, const Slot& slot, const Sources&... sources)
            {
                process(entity, m_array[slot.Get()], sources...);
            });

            m_array.Upload(rhiCtx, Size());
        }

        //! High-water mark. Every live slot is below it, so it is also the upload length.
        uint32_t Size() const { return m_slots ? m_slots->Bound() : 0; }

    private:
        StagedArrayBuffer<Element> m_array;
        Ptr<SlotPool<Tag>>         m_slots;
    };
}
