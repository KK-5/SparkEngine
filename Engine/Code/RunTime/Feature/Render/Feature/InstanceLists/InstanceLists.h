#pragma once

#include <Base.h>

#include <EASTL/fixed_vector.h>

#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Context/RHIContext.h>

#include <Pass/PassCapabilities.h>

//! The buffers the scene is drawn by, from culling to the indirect draws: their names and how
//! they are laid out, defined once here so no pass reaches into another to learn them.
//!
//! InstanceCullingPass writes, per kind of object, the instances each view sees. An
//! InstanceBinning pass per drawing pass sorts them into that pass's DrawGroups, and the drawing
//! pass's DrawIndirect reads what it wrote a group at a time.
namespace Spark::Render::InstanceLists
{
    //! A view a list is culled for: the view and its row of g_Views.
    struct CulledView
    {
        RHI::RHIHandle m_view      = RHI::NullHandle;
        uint32_t       m_viewIndex = 0;
    };

    //! How a list's visible instances are laid out for a set of views. The culling pass that
    //! writes them and each binning pass that reads them work it out alike, so the two agree on
    //! which view is the k-th and where its part starts.
    struct CullingLayout
    {
        eastl::fixed_vector<CulledView, 16> m_views;   // in order; view k's part starts at k * m_maxPerView
        uint32_t m_slotCount    = 0;   // slots of g_Instances to go through, holes included
        uint32_t m_viewCapacity = 0;   // views the buffers have room for, at least one
        uint32_t m_maxPerView   = 0;   // entries in each view's part of the visible list
    };

    //! The layout for `views`. A view with no row in g_Views yet is left out: it has nothing
    //! to be tested against, and nothing is drawn under it this frame either. The capacities
    //! are rounded up a step at a time, so the buffers keep their size while views and
    //! instances come and go.
    CullingLayout LayOutCulling(RHI::RHIContext& rhiContext, const ViewHandleList& views);

    // The visible lists, written by InstanceCullingPass. VisibleInstances holds the g_Instances
    // slots each view sees, packed from the start of the view's part; VisibleCount a uint32 per
    // view, how many entries its part holds this frame.

    //! The opaque instances, for the main views.
    namespace Opaque
    {
        const RHI::AttachmentId& VisibleInstancesName();
        const RHI::AttachmentId& VisibleCountName();
    }

    //! The shadow casters, for the shadow views.
    namespace ShadowCaster
    {
        const RHI::AttachmentId& VisibleInstancesName();
        const RHI::AttachmentId& VisibleCountName();
    }

    // A drawing pass's groups. IndirectLayout is the table of the pass's PassIndirectLayout, which
    // its InstanceBinning pass imports. DrawArguments and DrawCount are what that pass writes:
    // DrawIndexedIndirectCommand records, PassIndirectLayout::m_recordsPerView of them per view, and
    // a uint32 per group and view, PassIndirectLayout::m_countsPerView of them per view, both marked
    // as each view's (.IndirectArgumentsOf).

    namespace DepthPre
    {
        const RHI::AttachmentId& IndirectLayoutName();
        const RHI::AttachmentId& DrawArgumentsName();
        const RHI::AttachmentId& DrawCountName();
    }

    namespace GBuffer
    {
        const RHI::AttachmentId& IndirectLayoutName();
        const RHI::AttachmentId& DrawArgumentsName();
        const RHI::AttachmentId& DrawCountName();
    }

    namespace Shadow
    {
        const RHI::AttachmentId& IndirectLayoutName();
        const RHI::AttachmentId& DrawArgumentsName();
        const RHI::AttachmentId& DrawCountName();
    }

    // The lists the CPU culls. Their draws are the lists', not a pass's: DrawListCullingSystem
    // writes them all into the one buffer carrying CpuCulledDrawArgumentsTag, each exactly as many
    // as are seen, and a pass drawing such a list imports it under DrawArgumentsName and draws it
    // with no count.

    namespace CpuCulled
    {
        const RHI::AttachmentId& DrawArgumentsName();
    }
}
