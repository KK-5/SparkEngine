#pragma once

#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include <Staging/ElementUpdateBuffer.h>
#include <Suballocation/ReservedBuffer.h>
#include <Suballocation/Suballocator.h>

#include "InstanceBinding.h"

namespace Spark::RHI
{
    class Device;
}

namespace Spark::Render
{
    //! g_Instances on the GPU: a reserved buffer of InstanceCapacity records, backed page by page
    //! as slots are taken, the buffer carrying InstancePoolTag. Each frame it is given every
    //! held slot's record and writes the ones that differ from what the GPU holds, through
    //! updates its owner's pass applies.
    //!
    //! A frame goes: slots taken, BeginUpdate, Set for every held slot, EndUpdate.
    class InstancePool
    {
    public:
        static constexpr uint64_t ByteCount = uint64_t(InstanceCapacity) * sizeof(InstanceData);

        //! Binds g_Instances on `bindings` once: the buffer never changes.
        bool Init(RHI::RHIContext& rhiCtx, RHI::Device& device, RHI::RHIHandle bindings);
        //! After the GPU is idle, and after every slot is gone.
        void Shutdown(RHI::RHIContext& rhiCtx);

        //! A slot; empty when every one is taken.
        Suballocation Allocate();

        //! Every frame, once the frame's slots are taken and before any Set: makes room for them.
        void BeginUpdate();

        //! A held slot's record this frame.
        void Set(uint32_t slot, const InstanceData& record);

        //! Every frame, after the last Set: stages the records that differ, for the pass to write.
        void EndUpdate();

        //! The buffer as the graph imports it.
        RHI::RHIHandle GetResource() const { return m_resource; }
        //! This frame's writes, for the pass that applies them.
        const ElementUpdateBuffer& GetUpdates() const { return m_updates; }

    private:
        Ptr<Suballocator>                       m_allocator;
        ReservedBuffer                          m_pool;
        eastl::vector<Suballocator::PageChange> m_pageChanges;
        RHI::RHIHandle                          m_resource = RHI::NullHandle;
        //! The ShaderBindings g_Instances is bound on.
        RHI::RHIHandle                          m_bindings = RHI::NullHandle;
        ElementUpdateBuffer                     m_updates;

        //! What the GPU holds in each slot, where m_known says it is known.
        eastl::vector<InstanceData> m_records;
        eastl::vector<uint8_t>      m_known;
        //! This frame's slots whose record differs from what the GPU holds.
        eastl::vector<uint32_t>     m_changedSlots;
    };
}
