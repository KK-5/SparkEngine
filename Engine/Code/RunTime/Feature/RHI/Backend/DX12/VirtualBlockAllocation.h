#pragma once

#include <Base.h>
#include <Object/Object.h>

#include "3rdParty/D3D12MA/D3D12MemAlloc.h"

namespace Spark::RHI::DX12
{
    //! A part of a D3D12MA::VirtualBlock, given back when the last reference goes: what
    //! D3D12MA::Allocation is to a heap, so it can be held in a Ptr and queued for release
    //! the same way.
    //!
    //! The block is not thread-safe: the last reference must go where nothing allocates from
    //! the block at the same time. A release queue's Collect at frame end is such a place.
    class VirtualBlockAllocation final : public Object
    {
    public:
        VirtualBlockAllocation(D3D12MA::VirtualBlock* block, D3D12MA::VirtualAllocation allocation)
            : m_block(block)
            , m_allocation(allocation)
        {
        }

        ~VirtualBlockAllocation() override
        {
            m_block->FreeAllocation(m_allocation);
        }

    private:
        // Keeps the block alive, so the part can be given back whenever this goes.
        Ptr<D3D12MA::VirtualBlock> m_block;
        D3D12MA::VirtualAllocation m_allocation;
    };
}
