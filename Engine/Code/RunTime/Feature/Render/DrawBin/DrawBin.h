#pragma once

#include <cstdint>

#include <EASTL/array.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Pipeline/PipelineState.h>

#include <Drawable/DrawMask.h>
#include <Pass/Pass.h>
#include <PipelineState/PipelineKey.h>
#include <PipelineState/PipelineVariant.h>

namespace Spark::Render
{
    // A DrawBin is the instances one pass draws with one PSO, in one indirect call per view.
    // Keys live in the RHIContext beside InstanceSlotCount, bins and assignments in the
    // PassContext beside their passes; what goes on the passes is in PassDrawListComponents.h.

    //! A bin's and an assignment's entities. PassContext entities, so the same type as Pass:
    //! the names are for the reader.
    using DrawBinHandle           = Pass;
    using DrawBinAssignmentHandle = Pass;
    inline constexpr DrawBinHandle NullDrawBin = NullPass;

    //! On a key's entity (with PipelineKey), written every frame: how many encoded instances
    //! carry the key, per draw-mask bit.
    struct KeyInstanceCount
    {
        eastl::array<uint32_t, DrawMaskBitCount> m_counts{};
    };

    //! On a key's entity once every pass that bins has an assignment for it.
    struct KeyBinned
    {
    };

    //! On a bin's entity, with the PipelineVariant all its instances draw with.
    struct DrawBinOf
    {
        Pass m_pass = NullPass;
    };

    //! On a bin's entity, written every frame. index orders the pass's bins (its count is
    //! the index-th of a view's); first is the bin's first record within a view's arguments.
    struct DrawBinRange
    {
        uint32_t m_index    = 0;
        uint32_t m_first    = 0;
        uint32_t m_capacity = 0;
    };

    //! On a bin's entity: the PSO its instances draw with, its pass's default PSO with the
    //! bin's variant on it. Built once, when the bin is new.
    struct DrawBinPipeline
    {
        Ptr<RHI::PipelineState> m_pso;
    };

    //! On an entity of its own: in pass, instances of key go to bin.
    struct DrawBinAssignment
    {
        Pass           m_pass = NullPass;
        RHI::RHIHandle m_key  = RHI::NullHandle;
        DrawBinHandle  m_bin  = NullDrawBin;
    };
}
