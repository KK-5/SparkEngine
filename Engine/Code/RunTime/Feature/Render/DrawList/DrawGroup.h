#pragma once

#include <cstdint>

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include <Drawable/DrawTag.h>
#include <Pass/Pass.h>
#include <PipelineState/PipelineVariant.h>

#include "DrawContext.h"

namespace Spark::Render
{
    // The indirect draws of each pass drawing a list. DrawGroupSystem groups a pass's entries and
    // writes the groups into the DrawContext; the render graph reads them as it expands a pass's
    // indirect draw into an item a group.

    //! One thing a pass draws in a list, with its variant in that pass: a key of a GPU-culled
    //! list, or a visible instance of a CPU-culled one.
    struct DrawEntry
    {
        PipelineVariant m_variant;
        //! Draws it takes: a key's instances, or 1 for an instance.
        uint32_t        m_count    = 1;
        //! The key's PipelineKeyIndex. A visible instance's entry leaves it.
        uint32_t        m_keyIndex = 0;
        //! The group it falls in, which GroupDrawEntries writes.
        uint32_t        m_groupIndex = 0;
    };

    //! One indirect draw of a pass under a view: neighbouring entries of one variant, so of one
    //! PSO in the pass, which the render graph builds from the pass's description with the
    //! variant. An element of the DrawContext.
    struct DrawGroup
    {
        Pass           m_pass  = NullPass;
        RHI::RHIHandle m_view  = RHI::NullHandle;
        //! Its place among the pass's under the view, which is also which of the view's counts is its.
        uint32_t       m_index = 0;

        PipelineVariant m_variant;

        //! Its part of the indirect arguments, a command a draw: where it starts, after what the
        //! groups before it make room for, from the start of the view's part of the argument
        //! buffer, or of the buffer when it has no part per view; and how many its entries take,
        //! which is as many as its draw reads at most.
        uint32_t m_firstDraw = 0;
        uint32_t m_maxDraws  = 0;
    };

    //! The order a pass's groups draw in: by priority, the rest only to make it total.
    bool DrawsBefore(const PipelineVariant& a, const PipelineVariant& b);

    //! Replaces groups with those of entries, leaving their pass and view to the caller, and
    //! writes each entry's group. Any first sorts the entries by DrawsBefore, so there is a
    //! group a variant; BackToFront keeps them in the order given, so a variant has a group
    //! each time it comes back.
    void GroupDrawEntries(eastl::span<DrawEntry> entries, DrawOrder order, eastl::vector<DrawGroup>& groups);

    //! Orders the DrawContext's groups by pass, view and index, which DrawGroupsOf relies on.
    void SortDrawGroups(DrawContext& drawCtx);

    //! The DrawGroups of a pass under a view, in index order.
    using DrawGroupRange = DrawElementRange<DrawGroup>;

    //! Found by halving the storage SortDrawGroups left, so empty when nothing is there.
    DrawGroupRange DrawGroupsOf(const DrawContext& drawCtx, Pass pass, RHI::RHIHandle view);
}
