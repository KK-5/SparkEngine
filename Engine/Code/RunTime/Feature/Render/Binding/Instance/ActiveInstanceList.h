#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include <Staging/ElementUpdateBuffer.h>
#include <Suballocation/ReservedBuffer.h>

#include "InstanceBinding.h"

namespace Spark::RHI
{
    class Device;
}

namespace Spark::Render
{
    //! The active list on the GPU: position -> g_Instances slot, the buffer carrying
    //! InstanceIDBufferTag. Each frame it is given the list and makes the GPU's the same,
    //! writing only the positions that differ, through updates its owner's pass applies.
    //!
    //! The entries are found by their position, so they are a prefix of the buffer and so are
    //! their pages: mapped as far as the list reaches, those past it unmapped at once, as the
    //! queue runs the unmapping after every read already submitted.
    class ActiveInstanceList
    {
    public:
        static constexpr uint64_t ByteCount = uint64_t(InstanceCapacity) * sizeof(uint32_t);

        bool Init(RHI::RHIContext& rhiCtx, RHI::Device& device);
        //! After the GPU is idle.
        void Shutdown(RHI::RHIContext& rhiCtx);

        //! Makes the GPU's list `slots`; how many of its entries the GPU holds this frame.
        uint32_t Update(eastl::span<const uint32_t> slots);

        //! The buffer as the graph imports it.
        RHI::RHIHandle GetResource() const { return m_resource; }
        //! This frame's writes, for the pass that applies them.
        const ElementUpdateBuffer& GetUpdates() const { return m_updates; }

    private:
        //! Maps the pages `count` entries reach and unmaps the rest; how many entries are backed.
        uint32_t MapPages(uint32_t count);

        ReservedBuffer                          m_pool;
        eastl::vector<Suballocator::PageChange> m_pageChanges;
        RHI::RHIHandle                          m_resource = RHI::NullHandle;
        ElementUpdateBuffer                     m_updates;

        //! What the GPU holds at each position, as far as it is known.
        eastl::vector<uint32_t> m_mirror;
        //! This frame's positions whose slot differs from the mirror.
        eastl::vector<uint32_t> m_changedPositions;
    };
}
