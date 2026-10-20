#pragma once

#include <cstdint>

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <Drawable/DrawTag.h>
#include <PipelineState/PipelineVariant.h>

namespace Spark::Render
{
    //! One thing a pass draws in a list, with its variant in that pass: a key of a GPU-culled
    //! list, or a visible instance of a CPU-culled one.
    struct DrawEntry
    {
        PipelineVariant m_variant;
        //! Draws it takes: a key's instances, or 1 for an instance.
        uint32_t        m_count    = 1;
        //! The key's PipelineKeyIndex, to find it again once Any has reordered the entries.
        //! A visible instance's entry leaves it: BackToFront keeps the order.
        uint32_t        m_keyIndex = 0;
    };

    //! Neighbouring entries of one variant, so of one PSO in the pass: one indirect draw.
    struct DrawGroup
    {
        PipelineVariant m_variant;
        //! The entries it is made of, as GroupDrawEntries left them.
        uint32_t m_firstEntry = 0;
        uint32_t m_entryCount = 0;
        //! Its part of the indirect arguments, a command a draw: where it starts, after what
        //! the groups before it make room for, and how many its entries take, which is as many
        //! as its draw reads at most.
        uint32_t m_firstDraw = 0;
        uint32_t m_maxDraws  = 0;
    };

    //! The order a pass's groups draw in: by priority, the rest only to make it total.
    bool DrawsBefore(const PipelineVariant& a, const PipelineVariant& b);

    //! Replaces groups with those of entries. Any first sorts the entries by DrawsBefore, so
    //! there is a group a variant; BackToFront keeps them in the order given, so a variant has
    //! a group each time it comes back.
    void GroupDrawEntries(eastl::span<DrawEntry> entries, DrawOrder order, eastl::vector<DrawGroup>& groups);
}
