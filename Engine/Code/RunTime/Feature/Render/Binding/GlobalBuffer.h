#pragma once

#include <EASTL/functional.h>
#include <EASTL/heap.h>
#include <EASTL/vector.h>

#include <CoreComponents/Tags.h>
#include <ECS/BasicContext.h>
#include <Log/ILogSystem.h>
#include <Object/ObjectName.h>

#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

#include <Shader/ShaderBindingsUtils.h>


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

    //! One global, shader-visible array: a host per-frame StructuredBuffer plus the CPU
    //! mirror it is uploaded from. Sources are the components an entity must carry to own
    //! a slot, Element is the GPU record.
    //!
    //! Addressed by stable slot, so the array has holes and the upload spans [0, Size()).
    //! The mirror is the one complete image, which is what lets every frame copy it whole:
    //! a partial copy would leave each in-flight copy missing the changes made while it
    //! was not the write target, and a stopped object would oscillate between its old and
    //! new record with a period of frameCountMax.
    //!
    //! The owner creates the ShaderBindings entity (shader reflection, space id, its own
    //! tags) and hands it in — this only binds the SRV onto it each frame.
    template<typename Tag, typename Element, typename... Sources>
    class GlobalBuffer
    {
    public:
        struct Descriptor
        {
            uint32_t       m_capacity       = 0;
            ObjectName     m_resourceName;
            RHI::InputName m_inputName;
            RHI::RHIHandle m_bindingsEntity = RHI::NullHandle;
        };

        void Init(RHI::RHIContext& rhiCtx, const Descriptor& descriptor)
        {
            m_descriptor = descriptor;
            m_staging.resize(descriptor.m_capacity);
            m_versions.assign(descriptor.m_capacity, 0);

            RHI::BufferDescriptor bufDesc;
            bufDesc.m_byteCount = static_cast<uint64_t>(descriptor.m_capacity) * sizeof(Element);
            bufDesc.m_bindFlags = RHI::BufferBindFlags::ShaderRead | RHI::BufferBindFlags::CopyRead;

            RHI::PendingBufferInit init;
            init.m_descriptor       = bufDesc;
            init.m_heapMemoryLevel  = RHI::HeapMemoryLevel::Host;
            init.m_hostMemoryAccess = RHI::HostMemoryAccess::Write;

            m_buffer = rhiCtx.CreateEntity();
            rhiCtx.Add<RHI::PendingBufferInit>(m_buffer, init);
            rhiCtx.Add<RHI::PerFrameTag>(m_buffer);
            rhiCtx.Add<RHI::ResourceName>(m_buffer, RHI::ResourceName{ descriptor.m_resourceName });
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

            if (m_buffer != RHI::NullHandle)
            {
                rhiCtx.Add<DeadTag>(m_buffer);
                m_buffer = RHI::NullHandle;
            }
        }

        //! process: void(Entity, Element&, const Sources&...)
        template<typename Ctx, typename ProcessFn>
        void Update(Ctx& ctx, RHI::RHIContext& rhiCtx, uint32_t frameIndex, ProcessFn&& process)
        {
            using Slot = GlobalBufferSlotRef<Tag>;

            // Nothing is allocated or encoded until the buffer materializes, so the first
            // successful frame still carries every entity: the mirror is complete by
            // construction, not by replaying the warmup frames.
            if (!BindFrame(rhiCtx, frameIndex))
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
                process(entity, m_staging[slot.m_id], sources...);
            });

            if (m_nextFreshId > 0)
            {
                rhiCtx.AddOrReplace<RHI::PendingBufferMap>(
                    m_buffer,
                    RHI::PendingBufferMap{ m_staging.data(), 0,
                                           static_cast<size_t>(m_nextFreshId) * sizeof(Element) });
            }
        }

        //! High-water mark. Every live slot is below it, so it is also the upload length.
        uint32_t Size() const { return m_nextFreshId; }

    private:
        //! Re-points the SRV at this frame's copy. The descriptor encodes one buffer
        //! address and the copies live at distinct addresses, so this runs every frame.
        bool BindFrame(RHI::RHIContext& rhiCtx, uint32_t frameIndex)
        {
            if (m_buffer == RHI::NullHandle || m_descriptor.m_bindingsEntity == RHI::NullHandle)
            {
                return false;
            }

            auto* perFrame = rhiCtx.TryGet<RHI::Components::BufferPerFrame>(m_buffer);
            if (!perFrame || !perFrame->m_buffers[frameIndex])
            {
                return false;
            }

            const RHI::BufferViewDescriptor viewDesc = RHI::BufferViewDescriptor::CreateStructured(0, m_descriptor.m_capacity, sizeof(Element));
            RHI::BufferView* view = RHI::GetOrCreateBufferViewPerFrame(
                rhiCtx, m_buffer, *perFrame->m_buffers[frameIndex], viewDesc, frameIndex);
            if (!view)
            {
                LOG_ERROR("[GlobalBuffer] Failed to create structured SRV for '{}' frame {}.",
                          m_descriptor.m_resourceName.GetCStr(), frameIndex);
                return false;
            }

            SetShaderBuffer(m_descriptor.m_bindingsEntity, m_descriptor.m_inputName, view);
            return true;
        }

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

            if (m_nextFreshId >= m_descriptor.m_capacity)
            {
                LOG_ERROR("[GlobalBuffer] '{}' capacity={} overflow; dropping entity.",
                          m_descriptor.m_resourceName.GetCStr(), m_descriptor.m_capacity);
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

        Descriptor              m_descriptor;
        eastl::vector<Element>  m_staging;
        RHI::RHIHandle          m_buffer      = RHI::NullHandle;
        uint32_t                m_nextFreshId = 0;
        eastl::vector<uint32_t> m_freeIds;

        //! Occupancy counter per id, referenced by every GlobalBufferSlotRef handed out.
        //! Never reallocated while references are live (fixed at Init).
        eastl::vector<uint32_t> m_versions;
    };
}
