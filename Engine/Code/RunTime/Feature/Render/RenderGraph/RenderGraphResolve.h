#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>
#include <Pass/PassContext.h>

//! Which version of its resource each attachment of a frame reads or leaves, and the order the
//! passes run in for it. Reads and writes the two contexts only, no device, so it is tested on
//! contexts of its own.
namespace Spark::Render
{
    //! m_from runs before m_to.
    struct GraphEdge
    {
        Pass m_from {NullPass};
        Pass m_to {NullPass};

        bool operator==(const GraphEdge& other) const
        {
            return m_from == other.m_from && m_to == other.m_to;
        }
    };

    enum class GraphErrorType : uint8_t
    {
        //! A read of a transient resource that no pass writes, or only the reader's own pass
        //! after the read.
        ReadsUndefined,

        //! An access with .From(P), of a resource P has no write of declared before the
        //! access: P does not write it this frame, or is not declared before the access's pass.
        FromPassWritesNothing,
    };

    struct GraphError
    {
        GraphErrorType m_type {GraphErrorType::ReadsUndefined};
        RHI::RHIHandle m_attachment {RHI::NullHandle};
    };

    struct GraphResolution
    {
        //! What is wrong with the attachments themselves. The rest is still filled: a read of
        //! nothing is left at the version it would otherwise have, a .From that finds no write
        //! is taken as not there.
        eastl::vector<GraphError> m_errors;

        //! Each once, by the declaration order of m_from, then of m_to; none from a pass to
        //! itself.
        eastl::vector<GraphEdge> m_edges;

        //! Empty unless the edges form a cycle: the passes on it and those waiting for it,
        //! which get no PassGlobalTimeline.
        eastl::vector<Pass> m_unordered;
    };

    //! `attachments` are the frame's, in the order they were declared (by pass, by Scope within
    //! a pass, as written within a Scope), each linked to its resource
    //! (ScopeAttachment::m_resource); one that is not linked is left out.
    //!
    //! Gives each its version (AttachmentId::m_version). A resource starts at version 0 and
    //! every write leaves the next. A read sees what the writes declared before it left; with
    //! none before it, of a transient resource, which starts with nothing in it, what the
    //! frame's last write leaves.
    //!
    //! An attachment with a FromPass takes the resource as that pass left it instead. A read
    //! sees the version of that pass's last write. A write goes right after that write, and
    //! after the writes that were put there before it: the writes and reads that followed it
    //! get this one's version.
    //!
    //! Gives each pass with an attachment its place in the frame (PassGlobalTimeline): of the
    //! passes the edges leave ready, the one declared first runs first.
    void ResolveGraph(
        RHI::RHIContext& rhiContext, PassContext& passContext, eastl::span<const RHI::RHIHandle> attachments,
        GraphResolution& out);
}
